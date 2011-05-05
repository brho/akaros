/* See COPYRIGHT for copyright information. */

#ifdef __SHARC__
#pragma nosharc
#endif

#include <ros/common.h>
#include <arch/types.h>
#include <arch/arch.h>
#include <arch/mmu.h>
#include <arch/console.h>
#include <ros/timer.h>
#include <error.h>

#include <elf.h>
#include <string.h>
#include <assert.h>
#include <process.h>
#include <schedule.h>
#include <pmap.h>
#include <umem.h>
#include <mm.h>
#include <trap.h>
#include <syscall.h>
#include <kmalloc.h>
#include <stdio.h>
#include <resource.h>
#include <frontend.h>
#include <colored_caches.h>
#include <hashtable.h>
#include <bitmask.h>
#include <vfs.h>
#include <devfs.h>
#include <smp.h>
#include <arsc_server.h>
#include <event.h>


#ifdef __CONFIG_NETWORKING__
#include <arch/nic_common.h>
extern int (*send_frame)(const char *CT(len) data, size_t len);
extern unsigned char device_mac[6];
#endif

/* Tracing Globals */
int systrace_flags = 0;
struct systrace_record *systrace_buffer = 0;
unsigned int systrace_bufidx = 0;
size_t systrace_bufsize = 0;
struct proc *systrace_procs[MAX_NUM_TRACED] = {0};
spinlock_t systrace_lock = SPINLOCK_INITIALIZER;

/* Not enforcing the packing of systrace_procs yet, but don't rely on that */
static bool proc_is_traced(struct proc *p)
{
	for (int i = 0; i < MAX_NUM_TRACED; i++)
		if (systrace_procs[i] == p)
			return true;
	return false;
}

/* Helper that "finishes" the current async syscall.  This should be used when
 * we are calling a function in a syscall that might not return and won't be
 * able to use the normal syscall return path, such as proc_yield() and
 * resource_req().  Call this from within syscall.c (I don't want it global).
 *
 * It is possible for another user thread to see the syscall being done early -
 * they just need to be careful with the weird proc management calls (as in,
 * don't trust an async fork).
 *
 * *sysc is in user memory, and should be pinned (TODO: UMEM).  There may be
 * issues with unpinning this if we never return. */
static void signal_current_sc(int retval)
{
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	assert(pcpui->cur_sysc);
	pcpui->cur_sysc->retval = retval;
	atomic_or(&pcpui->cur_sysc->flags, SC_DONE); 
}

/* Callable by any function while executing a syscall (or otherwise, actually).
 */
void set_errno(int errno)
{
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	if (pcpui->cur_sysc)
		pcpui->cur_sysc->err = errno;
}

/************** Utility Syscalls **************/

static int sys_null(void)
{
	return 0;
}

/* Diagnostic function: blocks the kthread/syscall, to help userspace test its
 * async I/O handling. */
static int sys_block(struct proc *p, unsigned int usec)
{
	struct timer_chain *tchain = &per_cpu_info[core_id()].tchain;
	struct alarm_waiter a_waiter;
	init_awaiter(&a_waiter, 0);
	/* Note printing takes a few ms, so your printds won't be perfect. */
	printd("[kernel] sys_block(), sleeping at %llu\n", read_tsc());
	set_awaiter_rel(&a_waiter, usec);
	set_alarm(tchain, &a_waiter);
	sleep_on_awaiter(&a_waiter);
	printd("[kernel] sys_block(), waking up at %llu\n", read_tsc());
	return 0;
}

// Writes 'val' to 'num_writes' entries of the well-known array in the kernel
// address space.  It's just #defined to be some random 4MB chunk (which ought
// to be boot_alloced or something).  Meant to grab exclusive access to cache
// lines, to simulate doing something useful.
static int sys_cache_buster(struct proc *p, uint32_t num_writes,
                             uint32_t num_pages, uint32_t flags)
{ TRUSTEDBLOCK /* zra: this is not really part of the kernel */
	#define BUSTER_ADDR		0xd0000000  // around 512 MB deep
	#define MAX_WRITES		1048576*8
	#define MAX_PAGES		32
	#define INSERT_ADDR 	(UINFO + 2*PGSIZE) // should be free for these tests
	uint32_t* buster = (uint32_t*)BUSTER_ADDR;
	static spinlock_t buster_lock = SPINLOCK_INITIALIZER;
	uint64_t ticks = -1;
	page_t* a_page[MAX_PAGES];

	/* Strided Accesses or Not (adjust to step by cachelines) */
	uint32_t stride = 1;
	if (flags & BUSTER_STRIDED) {
		stride = 16;
		num_writes *= 16;
	}

	/* Shared Accesses or Not (adjust to use per-core regions)
	 * Careful, since this gives 8MB to each core, starting around 512MB.
	 * Also, doesn't separate memory for core 0 if it's an async call.
	 */
	if (!(flags & BUSTER_SHARED))
		buster = (uint32_t*)(BUSTER_ADDR + core_id() * 0x00800000);

	/* Start the timer, if we're asked to print this info*/
	if (flags & BUSTER_PRINT_TICKS)
		ticks = start_timing();

	/* Allocate num_pages (up to MAX_PAGES), to simulate doing some more
	 * realistic work.  Note we don't write to these pages, even if we pick
	 * unshared.  Mostly due to the inconvenience of having to match up the
	 * number of pages with the number of writes.  And it's unnecessary.
	 */
	if (num_pages) {
		spin_lock(&buster_lock);
		for (int i = 0; i < MIN(num_pages, MAX_PAGES); i++) {
			upage_alloc(p, &a_page[i],1);
			page_insert(p->env_pgdir, a_page[i], (void*)INSERT_ADDR + PGSIZE*i,
			            PTE_USER_RW);
			page_decref(a_page[i]);
		}
		spin_unlock(&buster_lock);
	}

	if (flags & BUSTER_LOCKED)
		spin_lock(&buster_lock);
	for (int i = 0; i < MIN(num_writes, MAX_WRITES); i=i+stride)
		buster[i] = 0xdeadbeef;
	if (flags & BUSTER_LOCKED)
		spin_unlock(&buster_lock);

	if (num_pages) {
		spin_lock(&buster_lock);
		for (int i = 0; i < MIN(num_pages, MAX_PAGES); i++) {
			page_remove(p->env_pgdir, (void*)(INSERT_ADDR + PGSIZE * i));
			page_decref(a_page[i]);
		}
		spin_unlock(&buster_lock);
	}

	/* Print info */
	if (flags & BUSTER_PRINT_TICKS) {
		ticks = stop_timing(ticks);
		printk("%llu,", ticks);
	}
	return 0;
}

static int sys_cache_invalidate(void)
{
	#ifdef __i386__
		wbinvd();
	#endif
	return 0;
}

/* sys_reboot(): called directly from dispatch table. */

/* Print a string to the system console. */
static ssize_t sys_cputs(struct proc *p, const char *DANGEROUS string,
                         size_t strlen)
{
	char *t_string;
	t_string = user_strdup_errno(p, string, strlen);
	if (!t_string)
		return -1;
	printk("%.*s", strlen, t_string);
	user_memdup_free(p, t_string);
	return (ssize_t)strlen;
}

// Read a character from the system console.
// Returns the character.
static uint16_t sys_cgetc(struct proc *p)
{
	uint16_t c;

	// The cons_getc() primitive doesn't wait for a character,
	// but the sys_cgetc() system call does.
	while ((c = cons_getc()) == 0)
		cpu_relax();

	return c;
}

/* Returns the id of the cpu this syscall is executed on. */
static uint32_t sys_getcpuid(void)
{
	return core_id();
}

// TODO: Temporary hack until thread-local storage is implemented on i386 and
// this is removed from the user interface
static size_t sys_getvcoreid(struct proc *p)
{
	return proc_get_vcoreid(p, core_id());
}

/************** Process management syscalls **************/

/* Returns the calling process's pid */
static pid_t sys_getpid(struct proc *p)
{
	return p->pid;
}

/* Creates a process from the file 'path'.  The process is not runnable by
 * default, so it needs it's status to be changed so that the next call to
 * schedule() will try to run it.  TODO: take args/envs from userspace. */
static int sys_proc_create(struct proc *p, char *path, size_t path_l,
                           struct procinfo *pi)
{
	int pid = 0;
	char *t_path;
	struct file *program;
	struct proc *new_p;

	/* Copy in the path.  Consider putting an upper bound on path_l. */
	t_path = user_strdup_errno(p, path, path_l);
	if (!t_path)
		return -1;
	program = do_file_open(t_path, 0, 0);
	user_memdup_free(p, t_path);
	if (!program)
		return -1;			/* presumably, errno is already set */
	/* TODO: need to split the proc creation, since you must load after setting
	 * args/env, since auxp gets set up there. */
	//new_p = proc_create(program, 0, 0);
	if (proc_alloc(&new_p, current))
		goto mid_error;
	/* Set the argument stuff needed by glibc */
	if (memcpy_from_user_errno(p, new_p->procinfo->argp, pi->argp,
	                           sizeof(pi->argp)))
		goto late_error;
	if (memcpy_from_user_errno(p, new_p->procinfo->argbuf, pi->argbuf,
	                           sizeof(pi->argbuf)))
		goto late_error;
	if (load_elf(new_p, program))
		goto late_error;
	kref_put(&program->f_kref);
	/* Connect to stdin, stdout, stderr (part of proc_create()) */
	assert(insert_file(&new_p->open_files, dev_stdin,  0) == 0);
	assert(insert_file(&new_p->open_files, dev_stdout, 0) == 1);
	assert(insert_file(&new_p->open_files, dev_stderr, 0) == 2);
	__proc_ready(new_p);
	pid = new_p->pid;
	proc_decref(new_p);	/* give up the reference created in proc_create() */
	return pid;
late_error:
	proc_destroy(new_p);
mid_error:
	kref_put(&program->f_kref);
	return -1;
}

/* Makes process PID runnable.  Consider moving the functionality to process.c */
static error_t sys_proc_run(struct proc *p, unsigned pid)
{
	struct proc *target = pid2proc(pid);
	error_t retval = 0;

	if (!target)
		return -EBADPROC;
 	// note we can get interrupted here. it's not bad.
	spin_lock(&p->proc_lock);
	// make sure we have access and it's in the right state to be activated
	if (!proc_controls(p, target)) {
		proc_decref(target);
		retval = -EPERM;
	} else if (target->state != PROC_CREATED) {
		proc_decref(target);
		retval = -EINVAL;
	} else {
		__proc_set_state(target, PROC_RUNNABLE_S);
		schedule_proc(target);
	}
	spin_unlock(&p->proc_lock);
	proc_decref(target);
	return retval;
}

/* Destroy proc pid.  If this is called by the dying process, it will never
 * return.  o/w it will return 0 on success, or an error.  Errors include:
 * - EBADPROC: if there is no such process with pid
 * - EPERM: if caller does not control pid */
static error_t sys_proc_destroy(struct proc *p, pid_t pid, int exitcode)
{
	error_t r;
	struct proc *p_to_die = pid2proc(pid);

	if (!p_to_die) {
		set_errno(ESRCH);
		return -1;
	}
	if (!proc_controls(p, p_to_die)) {
		proc_decref(p_to_die);
		set_errno(EPERM);
		return -1;
	}
	if (p_to_die == p) {
		p->exitcode = exitcode;
		printd("[PID %d] proc exiting gracefully (code %d)\n", p->pid,exitcode);
	} else {
		p_to_die->exitcode = exitcode; 	/* so its parent has some clue */
		printd("[%d] destroying proc %d\n", p->pid, p_to_die->pid);
	}
	proc_destroy(p_to_die);
	/* we only get here if we weren't the one to die */
	proc_decref(p_to_die);
	return ESUCCESS;
}

static int sys_proc_yield(struct proc *p, bool being_nice)
{
	/* proc_yield() often doesn't return - we need to set the syscall retval
	 * early.  If it doesn't return, it expects to eat our reference (for now).
	 */
	signal_current_sc(0);
	proc_incref(p, 1);
	proc_yield(p, being_nice);
	proc_decref(p);
	return 0;
}

static ssize_t sys_fork(env_t* e)
{
	// TODO: right now we only support fork for single-core processes
	if (e->state != PROC_RUNNING_S) {
		set_errno(EINVAL);
		return -1;
	}
	env_t* env;
	assert(!proc_alloc(&env, current));
	assert(env != NULL);

	env->heap_top = e->heap_top;
	env->ppid = e->pid;
	/* Can't really fork if we don't have a current_tf to fork */
	if (!current_tf) {
		set_errno(EINVAL);
		return -1;
	}
	env->env_tf = *current_tf;

	/* We need to speculatively say the syscall worked before copying the memory
	 * out, since the 'forked' process's call never actually goes through the
	 * syscall return path, and will never think it is done.  This violates a
	 * few things.  Just be careful with fork. */
	signal_current_sc(0);

	env->cache_colors_map = cache_colors_map_alloc();
	for(int i=0; i < llc_cache->num_colors; i++)
		if(GET_BITMASK_BIT(e->cache_colors_map,i))
			cache_color_alloc(llc_cache, env->cache_colors_map);

	duplicate_vmrs(e, env);

	int copy_page(env_t* e, pte_t* pte, void* va, void* arg)
	{
		env_t* env = (env_t*)arg;

		if(PAGE_PRESENT(*pte))
		{
			page_t* pp;
			if(upage_alloc(env,&pp,0))
				return -1;
			if(page_insert(env->env_pgdir,pp,va,*pte & PTE_PERM))
			{
				page_decref(pp);
				return -1;
			}
			pagecopy(page2kva(pp),ppn2kva(PTE2PPN(*pte)));
			page_decref(pp);
		} else {
			assert(PAGE_PAGED_OUT(*pte));
			/* TODO: (SWAP) will need to either make a copy or CoW/refcnt the
			 * backend store.  For now, this PTE will be the same as the
			 * original PTE */
			panic("Swapping not supported!");
			pte_t* newpte = pgdir_walk(env->env_pgdir,va,1);
			if(!newpte)
				return -1;
			*newpte = *pte;
		}
		return 0;
	}

	// TODO: (PC) this won't work.  Needs revisiting.
	// copy procdata and procinfo
	memcpy(env->procdata,e->procdata,sizeof(struct procdata));
	memcpy(env->procinfo,e->procinfo,sizeof(struct procinfo));
	env->procinfo->pid = env->pid;
	env->procinfo->ppid = env->ppid;

	/* for now, just copy the contents of every present page in the entire
	 * address space. */
	if (env_user_mem_walk(e, 0, UMAPTOP, &copy_page, env)) {
		proc_destroy(env);	/* this is prob what you want, not decref by 2 */
		set_errno(ENOMEM);
		return -1;
	}
	clone_files(&e->open_files, &env->open_files);
	__proc_ready(env);
	__proc_set_state(env, PROC_RUNNABLE_S);
	schedule_proc(env);

	// don't decref the new process.
	// that will happen when the parent waits for it.
	// TODO: if the parent doesn't wait, we need to change the child's parent
	// when the parent dies, or at least decref it

	printd("[PID %d] fork PID %d\n",e->pid,env->pid);
	return env->pid;
}

/* Load the binary "path" into the current process, and start executing it.
 * argv and envp are magically bundled in procinfo for now.  Keep in sync with
 * glibc's sysdeps/ros/execve.c.  Once past a certain point, this function won't
 * return.  It assumes (and checks) that it is current.  Don't give it an extra
 * refcnt'd *p (syscall won't do that). 
 * Note: if someone batched syscalls with this call, they could clobber their
 * old memory (and will likely PF and die).  Don't do it... */
static int sys_exec(struct proc *p, char *path, size_t path_l,
                    struct procinfo *pi)
{
	int ret = -1;
	char *t_path;
	struct file *program;
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	struct trapframe *old_cur_tf = pcpui->cur_tf;

	/* We probably want it to never be allowed to exec if it ever was _M */
	if (p->state != PROC_RUNNING_S) {
		set_errno(EINVAL);
		return -1;
	}
	if (p != pcpui->cur_proc) {
		set_errno(EINVAL);
		return -1;
	}
	/* Can't exec if we don't have a current_tf to restart (if we fail).  This
	 * isn't 100% true, but I'm okay with it. */
	if (!old_cur_tf) {
		set_errno(EINVAL);
		return -1;
	}
	/* Copy in the path.  Consider putting an upper bound on path_l. */
	t_path = user_strdup_errno(p, path, path_l);
	if (!t_path)
		return -1;
	/* Clear the current_tf.  We won't be returning the 'normal' way.  Even if
	 * we want to return with an error, we need to go back differently in case
	 * we succeed.  This needs to be done before we could possibly block, but
	 * unfortunately happens before the point of no return. */
	pcpui->cur_tf = 0;
	/* This could block: */
	program = do_file_open(t_path, 0, 0);
	user_memdup_free(p, t_path);
	if (!program)
		goto early_error;
	/* Set the argument stuff needed by glibc */
	if (memcpy_from_user_errno(p, p->procinfo->argp, pi->argp,
	                           sizeof(pi->argp)))
		goto mid_error;
	if (memcpy_from_user_errno(p, p->procinfo->argbuf, pi->argbuf,
	                           sizeof(pi->argbuf)))
		goto mid_error;
	/* This is the point of no return for the process. */
	/* TODO: issues with this: Need to also assert there are no outstanding
	 * users of the sysrings.  the ldt page will get freed shortly, so that's
	 * okay.  Potentially issues with the nm and vcpd if we were in _M before
	 * and someone is trying to notify. */
	memset(p->procdata, 0, sizeof(procdata_t));
	destroy_vmrs(p);
	close_all_files(&p->open_files, TRUE);
	env_user_mem_free(p, 0, UMAPTOP);
	if (load_elf(p, program)) {
		kref_put(&program->f_kref);
		/* Need an edible reference for proc_destroy in case it doesn't return.
		 * sys_exec was given current's ref (counted once just for current) */
		proc_incref(p, 1);
		proc_destroy(p);
		proc_decref(p);
		/* We don't want to do anything else - we just need to not accidentally
		 * return to the user (hence the all_out) */
		goto all_out;
	}
	printd("[PID %d] exec %s\n", p->pid, file_name(program));
	kref_put(&program->f_kref);
	goto success;
	/* These error and out paths are so we can handle the async interface, both
	 * for when we want to error/return to the proc, as well as when we succeed
	 * and want to start the newly exec'd _S */
mid_error:
	/* These two error paths are for when we want to restart the process with an
	 * error value (errno is already set). */
	kref_put(&program->f_kref);
early_error:
	p->env_tf = *old_cur_tf;
	signal_current_sc(-1);
success:
	/* Here's how we'll restart the new (or old) process: */
	spin_lock(&p->proc_lock);
	__proc_set_state(p, PROC_RUNNABLE_S);
	schedule_proc(p);
	spin_unlock(&p->proc_lock);
all_out:
	/* we can't return, since we'd write retvals to the old location of the
	 * sycall struct (which has been freed and is in the old userspace) (or has
	 * already been written to).*/
	abandon_core();
	smp_idle();
	assert(0);
}

static ssize_t sys_trywait(env_t* e, pid_t pid, int* status)
{
	struct proc* p = pid2proc(pid);

	// TODO: this syscall is racy, so we only support for single-core procs
	if(e->state != PROC_RUNNING_S)
		return -1;

	// TODO: need to use errno properly.  sadly, ROS error codes conflict..

	if(p)
	{
		ssize_t ret;

		if(current->pid == p->ppid)
		{
			if(p->state == PROC_DYING)
			{
				memcpy_to_user(e,status,&p->exitcode,sizeof(int));
				printd("[PID %d] waited for PID %d (code %d)\n",
				       e->pid,p->pid,p->exitcode);
				ret = 0;
			}
			else // not dead yet
			{
				set_errno(ESUCCESS);
				ret = -1;
			}
		}
		else // not a child of the calling process
		{
			set_errno(EPERM);
			ret = -1;
		}

		// if the wait succeeded, decref twice
		if (ret == 0)
			proc_decref(p);
		proc_decref(p);
		return ret;
	}

	set_errno(EPERM);
	return -1;
}

/************** Memory Management Syscalls **************/

static void *sys_mmap(struct proc *p, uintptr_t addr, size_t len, int prot,
                      int flags, int fd, off_t offset)
{
	return mmap(p, addr, len, prot, flags, fd, offset);
}

static intreg_t sys_mprotect(struct proc *p, void *addr, size_t len, int prot)
{
	return mprotect(p, (uintptr_t)addr, len, prot);
}

static intreg_t sys_munmap(struct proc *p, void *addr, size_t len)
{
	return munmap(p, (uintptr_t)addr, len);
}

static ssize_t sys_shared_page_alloc(env_t* p1,
                                     void**DANGEROUS _addr, pid_t p2_id,
                                     int p1_flags, int p2_flags
                                    )
{
	printk("[kernel] shared page alloc is deprecated/unimplemented.\n");
	return -1;
}

static int sys_shared_page_free(env_t* p1, void*DANGEROUS addr, pid_t p2)
{
	return -1;
}


static int sys_resource_req(struct proc *p, int type, unsigned int amt_wanted,
                            unsigned int amt_wanted_min, int flags)
{
	int retval;
	signal_current_sc(0);
	/* this might not return (if it's a _S -> _M transition) */
	proc_incref(p, 1);
	retval = resource_req(p, type, amt_wanted, amt_wanted_min, flags);
	proc_decref(p);
	return retval;
}

/* Untested.  Will notify the target on the given vcore, if the caller controls
 * the target.  Will honor the target's wanted/vcoreid.  u_ne can be NULL. */
static int sys_notify(struct proc *p, int target_pid, unsigned int ev_type,
                      struct event_msg *u_msg)
{
	struct event_msg local_msg = {0};
	struct proc *target = pid2proc(target_pid);
	if (!target) {
		set_errno(EBADPROC);
		return -1;
	}
	if (!proc_controls(p, target)) {
		proc_decref(target);
		set_errno(EPERM);
		return -1;
	}
	/* if the user provided an ev_msg, copy it in and use that */
	if (u_msg) {
		if (memcpy_from_user(p, &local_msg, u_msg, sizeof(struct event_msg))) {
			proc_decref(target);
			set_errno(EINVAL);
			return -1;
		}
	}
	send_kernel_event(target, &local_msg, 0);
	proc_decref(target);
	return 0;
}

/* Will notify the calling process on the given vcore, independently of WANTED
 * or advertised vcoreid.  If you change the parameters, change pop_ros_tf() */
static int sys_self_notify(struct proc *p, uint32_t vcoreid,
                           unsigned int ev_type, struct event_msg *u_msg)
{
	struct event_msg local_msg = {0};

	printd("[kernel] received self notify for vcoreid %d, type %d, msg %08p\n",
	       vcoreid, ev_type, u_msg);
	/* if the user provided an ev_msg, copy it in and use that */
	if (u_msg) {
		if (memcpy_from_user(p, &local_msg, u_msg, sizeof(struct event_msg))) {
			set_errno(EINVAL);
			return -1;
		}
	}
	/* this will post a message and IPI, regardless of wants/needs/debutantes.*/
	post_vcore_event(p, &local_msg, vcoreid);
	proc_notify(p, vcoreid);
	return 0;
}

/* This will set a local timer for usec, then shut down the core.  There's a
 * slight race between spinner and halt.  For now, the core will wake up for
 * other interrupts and service them, but will not process routine messages or
 * do anything other than halt until the alarm goes off.  We could just unset
 * the alarm and return early.  On hardware, there are a lot of interrupts that
 * come in.  If we ever use this, we can take a closer look.  */
static int sys_halt_core(struct proc *p, unsigned int usec)
{
	struct timer_chain *tchain = &per_cpu_info[core_id()].tchain;
	struct alarm_waiter a_waiter;
	bool spinner = TRUE;
	void unblock(struct alarm_waiter *waiter)
	{
		spinner = FALSE;
	}
	init_awaiter(&a_waiter, unblock);
	set_awaiter_rel(&a_waiter, MAX(usec, 100));
	set_alarm(tchain, &a_waiter);
	enable_irq();
	/* Could wake up due to another interrupt, but we want to sleep still. */
	while (spinner) {
		cpu_halt();	/* slight race between spinner and halt */
		cpu_relax();
	}
	printd("Returning from halting\n");
	return 0;
}

/************** Platform Specific Syscalls **************/

//Read a buffer over the serial port
static ssize_t sys_serial_read(env_t* e, char *DANGEROUS _buf, size_t len)
{
	printk("[kernel] serial reading is deprecated.\n");
	if (len == 0)
		return 0;

	#ifdef __CONFIG_SERIAL_IO__
	    char *COUNT(len) buf = user_mem_assert(e, _buf, len, PTE_USER_RO);
		size_t bytes_read = 0;
		int c;
		while((c = serial_read_byte()) != -1) {
			buf[bytes_read++] = (uint8_t)c;
			if(bytes_read == len) break;
		}
		return (ssize_t)bytes_read;
	#else
		return -EINVAL;
	#endif
}

//Write a buffer over the serial port
static ssize_t sys_serial_write(env_t* e, const char *DANGEROUS buf, size_t len)
{
	printk("[kernel] serial writing is deprecated.\n");
	if (len == 0)
		return 0;
	#ifdef __CONFIG_SERIAL_IO__
		char *COUNT(len) _buf = user_mem_assert(e, buf, len, PTE_USER_RO);
		for(int i =0; i<len; i++)
			serial_send_byte(buf[i]);
		return (ssize_t)len;
	#else
		return -EINVAL;
	#endif
}

#ifdef __CONFIG_NETWORKING__
// This is not a syscall we want. Its hacky. Here just for syscall stuff until get a stack.
static ssize_t sys_eth_read(env_t* e, char *DANGEROUS buf)
{
	if (eth_up) {

		uint32_t len;
		char *ptr;

		spin_lock(&packet_buffers_lock);

		if (num_packet_buffers == 0) {
			spin_unlock(&packet_buffers_lock);
			return 0;
		}

		ptr = packet_buffers[packet_buffers_head];
		len = packet_buffers_sizes[packet_buffers_head];

		num_packet_buffers--;
		packet_buffers_head = (packet_buffers_head + 1) % MAX_PACKET_BUFFERS;

		spin_unlock(&packet_buffers_lock);

		char* _buf = user_mem_assert(e, buf, len, PTE_U);

		memcpy(_buf, ptr, len);

		kfree(ptr);

		return len;
	}
	else
		return -EINVAL;
}

// This is not a syscall we want. Its hacky. Here just for syscall stuff until get a stack.
static ssize_t sys_eth_write(env_t* e, const char *DANGEROUS buf, size_t len)
{
	if (eth_up) {

		if (len == 0)
			return 0;

		// HACK TO BYPASS HACK
		int just_sent = send_frame(buf, len);

		if (just_sent < 0) {
			printk("Packet send fail\n");
			return 0;
		}

		return just_sent;

		// END OF RECURSIVE HACK
/*
		char *COUNT(len) _buf = user_mem_assert(e, buf, len, PTE_U);
		int total_sent = 0;
		int just_sent = 0;
		int cur_packet_len = 0;
		while (total_sent != len) {
			cur_packet_len = ((len - total_sent) > MTU) ? MTU : (len - total_sent);
			char dest_mac[6] = APPSERVER_MAC_ADDRESS;
			char* wrap_buffer = eth_wrap(_buf + total_sent, cur_packet_len, device_mac, dest_mac, APPSERVER_PORT);
			just_sent = send_frame(wrap_buffer, cur_packet_len + sizeof(struct ETH_Header));

			if (just_sent < 0)
				return 0; // This should be an error code of its own

			if (wrap_buffer)
				kfree(wrap_buffer);

			total_sent += cur_packet_len;
		}

		return (ssize_t)len;
*/
	}
	else
		return -EINVAL;
}

static ssize_t sys_eth_get_mac_addr(env_t* e, char *DANGEROUS buf) 
{
	if (eth_up) {
		for (int i = 0; i < 6; i++)
			buf[i] = device_mac[i];
		return 0;
	}
	else
		return -EINVAL;
}

static int sys_eth_recv_check(env_t* e) 
{
	if (num_packet_buffers != 0) 
		return 1;
	else
		return 0;
}

#endif // Network

static intreg_t sys_read(struct proc *p, int fd, void *buf, int len)
{
	ssize_t ret;
	struct file *file = get_file_from_fd(&p->open_files, fd);
	if (!file) {
		set_errno(EBADF);
		return -1;
	}
	if (!file->f_op->read) {
		kref_put(&file->f_kref);
		set_errno(EINVAL);
		return -1;
	}
	/* TODO: (UMEM) currently, read() handles user memcpy issues, but we
	 * probably should user_mem_check and pin the region here, so read doesn't
	 * worry about it */
	ret = file->f_op->read(file, buf, len, &file->f_pos);
	kref_put(&file->f_kref);
	return ret;
}

static intreg_t sys_write(struct proc *p, int fd, const void *buf, int len)
{
	ssize_t ret;
	struct file *file = get_file_from_fd(&p->open_files, fd);
	if (!file) {
		set_errno(EBADF);
		return -1;
	}
	if (!file->f_op->write) {
		kref_put(&file->f_kref);
		set_errno(EINVAL);
		return -1;
	}
	/* TODO: (UMEM) */
	ret = file->f_op->write(file, buf, len, &file->f_pos);
	kref_put(&file->f_kref);
	return ret;
}

/* Checks args/reads in the path, opens the file, and inserts it into the
 * process's open file list. 
 *
 * TODO: take the path length */
static intreg_t sys_open(struct proc *p, const char *path, size_t path_l,
                         int oflag, int mode)
{
	int fd = 0;
	struct file *file;

	printd("File %s Open attempt\n", path);
	char *t_path = user_strdup_errno(p, path, path_l);
	if (!t_path)
		return -1;
	mode &= ~p->fs_env.umask;
	file = do_file_open(t_path, oflag, mode);
	user_memdup_free(p, t_path);
	if (!file)
		return -1;
	fd = insert_file(&p->open_files, file, 0);	/* stores the ref to file */
	kref_put(&file->f_kref);
	if (fd < 0) {
		warn("File insertion failed");
		return -1;
	}
	printd("File %s Open, res=%d\n", path, fd);
	return fd;
}

static intreg_t sys_close(struct proc *p, int fd)
{
	struct file *file = put_file_from_fd(&p->open_files, fd);
	if (!file) {
		set_errno(EBADF);
		return -1;
	}
	return 0;
}

/* kept around til we remove the last ufe */
#define ufe(which,a0,a1,a2,a3) \
	frontend_syscall_errno(p,APPSERVER_SYSCALL_##which,\
	                   (int)(a0),(int)(a1),(int)(a2),(int)(a3))

static intreg_t sys_fstat(struct proc *p, int fd, struct kstat *u_stat)
{
	struct kstat *kbuf;
	struct file *file = get_file_from_fd(&p->open_files, fd);
	if (!file) {
		set_errno(EBADF);
		return -1;
	}
	kbuf = kmalloc(sizeof(struct kstat), 0);
	if (!kbuf) {
		kref_put(&file->f_kref);
		set_errno(ENOMEM);
		return -1;
	}
	stat_inode(file->f_dentry->d_inode, kbuf);
	kref_put(&file->f_kref);
	/* TODO: UMEM: pin the memory, copy directly, and skip the kernel buffer */
	if (memcpy_to_user_errno(p, u_stat, kbuf, sizeof(struct kstat))) {
		kfree(kbuf);
		set_errno(EINVAL);
		return -1;
	}
	kfree(kbuf);
	return 0;
}

/* sys_stat() and sys_lstat() do nearly the same thing, differing in how they
 * treat a symlink for the final item, which (probably) will be controlled by
 * the lookup flags */
static intreg_t stat_helper(struct proc *p, const char *path, size_t path_l,
                            struct kstat *u_stat, int flags)
{
	struct kstat *kbuf;
	struct dentry *path_d;
	char *t_path = user_strdup_errno(p, path, path_l);
	if (!t_path)
		return -1;
	path_d = lookup_dentry(t_path, flags);
	user_memdup_free(p, t_path);
	if (!path_d)
		return -1;
	kbuf = kmalloc(sizeof(struct kstat), 0);
	if (!kbuf) {
		set_errno(ENOMEM);
		kref_put(&path_d->d_kref);
		return -1;
	}
	stat_inode(path_d->d_inode, kbuf);
	kref_put(&path_d->d_kref);
	/* TODO: UMEM: pin the memory, copy directly, and skip the kernel buffer */
	if (memcpy_to_user_errno(p, u_stat, kbuf, sizeof(struct kstat))) {
		kfree(kbuf);
		set_errno(EINVAL);
		return -1;
	}
	kfree(kbuf);
	return 0;
}

/* Follow a final symlink */
static intreg_t sys_stat(struct proc *p, const char *path, size_t path_l,
                         struct kstat *u_stat)
{
	return stat_helper(p, path, path_l, u_stat, LOOKUP_FOLLOW);
}

/* Don't follow a final symlink */
static intreg_t sys_lstat(struct proc *p, const char *path, size_t path_l,
                          struct kstat *u_stat)
{
	return stat_helper(p, path, path_l, u_stat, 0);
}

intreg_t sys_fcntl(struct proc *p, int fd, int cmd, int arg)
{
	int retval = 0;
	struct file *file = get_file_from_fd(&p->open_files, fd);
	if (!file) {
		set_errno(EBADF);
		return -1;
	}
	switch (cmd) {
		case (F_DUPFD):
			retval = insert_file(&p->open_files, file, arg);
			if (retval < 0) {
				set_errno(-retval);
				retval = -1;
			}
			break;
		case (F_GETFD):
			retval = p->open_files.fd[fd].fd_flags;
			break;
		case (F_SETFD):
			if (arg == FD_CLOEXEC)
				file->f_flags |= O_CLOEXEC;
			break;
		case (F_GETFL):
			retval = file->f_flags;
			break;
		case (F_SETFL):
			/* only allowed to set certain flags. */
			arg &= O_FCNTL_FLAGS;
			file->f_flags = (file->f_flags & ~O_FCNTL_FLAGS) | arg;
			break;
		default:
			warn("Unsupported fcntl cmd %d\n", cmd);
	}
	kref_put(&file->f_kref);
	return retval;
}

static intreg_t sys_access(struct proc *p, const char *path, size_t path_l,
                           int mode)
{
	int retval;
	char *t_path = user_strdup_errno(p, path, path_l);
	if (!t_path)
		return -1;
	retval = do_access(t_path, mode);
	user_memdup_free(p, t_path);
	printd("Access for path: %s retval: %d\n", path, retval);
	if (retval < 0) {
		set_errno(-retval);
		return -1;
	}
	return retval;
}

intreg_t sys_umask(struct proc *p, int mask)
{
	int old_mask = p->fs_env.umask;
	p->fs_env.umask = mask & S_PMASK;
	return old_mask;
}

intreg_t sys_chmod(struct proc *p, const char *path, size_t path_l, int mode)
{
	int retval;
	char *t_path = user_strdup_errno(p, path, path_l);
	if (!t_path)
		return -1;
	retval = do_chmod(t_path, mode);
	user_memdup_free(p, t_path);
	if (retval < 0) {
		set_errno(-retval);
		return -1;
	}
	return retval;
}

static intreg_t sys_lseek(struct proc *p, int fd, off_t offset, int whence)
{
	off_t ret;
	struct file *file = get_file_from_fd(&p->open_files, fd);
	if (!file) {
		set_errno(EBADF);
		return -1;
	}
	ret = file->f_op->llseek(file, offset, whence);
	kref_put(&file->f_kref);
	return ret;
}

intreg_t sys_link(struct proc *p, char *old_path, size_t old_l,
                  char *new_path, size_t new_l)
{
	int ret;
	char *t_oldpath = user_strdup_errno(p, old_path, old_l);
	if (t_oldpath == NULL)
		return -1;
	char *t_newpath = user_strdup_errno(p, new_path, new_l);
	if (t_newpath == NULL) {
		user_memdup_free(p, t_oldpath);
		return -1;
	}
	ret = do_link(t_oldpath, t_newpath);
	user_memdup_free(p, t_oldpath);
	user_memdup_free(p, t_newpath);
	return ret;
}

intreg_t sys_unlink(struct proc *p, const char *path, size_t path_l)
{
	int retval;
	char *t_path = user_strdup_errno(p, path, path_l);
	if (!t_path)
		return -1;
	retval = do_unlink(t_path);
	user_memdup_free(p, t_path);
	return retval;
}

intreg_t sys_symlink(struct proc *p, char *old_path, size_t old_l,
                     char *new_path, size_t new_l)
{
	int ret;
	char *t_oldpath = user_strdup_errno(p, old_path, old_l);
	if (t_oldpath == NULL)
		return -1;
	char *t_newpath = user_strdup_errno(p, new_path, new_l);
	if (t_newpath == NULL) {
		user_memdup_free(p, t_oldpath);
		return -1;
	}
	ret = do_symlink(new_path, old_path, S_IRWXU | S_IRWXG | S_IRWXO);
	user_memdup_free(p, t_oldpath);
	user_memdup_free(p, t_newpath);
	return ret;
}

intreg_t sys_readlink(struct proc *p, char *path, size_t path_l,
                      char *u_buf, size_t buf_l)
{
	char *symname;
	ssize_t copy_amt;
	struct dentry *path_d;
	char *t_path = user_strdup_errno(p, path, path_l);
	if (t_path == NULL)
		return -1;
	path_d = lookup_dentry(t_path, 0);
	user_memdup_free(p, t_path);
	if (!path_d)
		return -1;
	symname = path_d->d_inode->i_op->readlink(path_d);
	copy_amt = strnlen(symname, buf_l - 1) + 1;
	if (memcpy_to_user_errno(p, u_buf, symname, copy_amt)) {
		kref_put(&path_d->d_kref);
		set_errno(EINVAL);
		return -1;
	}
	kref_put(&path_d->d_kref);
	printd("READLINK returning %s\n", u_buf);
	return copy_amt;
}

intreg_t sys_chdir(struct proc *p, const char *path, size_t path_l)
{
	int retval;
	char *t_path = user_strdup_errno(p, path, path_l);
	if (!t_path)
		return -1;
	retval = do_chdir(&p->fs_env, t_path);
	user_memdup_free(p, t_path);
	if (retval) {
		set_errno(-retval);
		return -1;
	}
	return 0;
}

/* Note cwd_l is not a strlen, it's an absolute size */
intreg_t sys_getcwd(struct proc *p, char *u_cwd, size_t cwd_l)
{
	int retval = 0;
	char *kfree_this;
	char *k_cwd = do_getcwd(&p->fs_env, &kfree_this, cwd_l);
	if (!k_cwd)
		return -1;		/* errno set by do_getcwd */
	if (memcpy_to_user_errno(p, u_cwd, k_cwd, strnlen(k_cwd, cwd_l - 1) + 1))
		retval = -1;
	kfree(kfree_this);
	return retval;
}

intreg_t sys_mkdir(struct proc *p, const char *path, size_t path_l, int mode)
{
	int retval;
	char *t_path = user_strdup_errno(p, path, path_l);
	if (!t_path)
		return -1;
	mode &= ~p->fs_env.umask;
	retval = do_mkdir(t_path, mode);
	user_memdup_free(p, t_path);
	return retval;
}

intreg_t sys_rmdir(struct proc *p, const char *path, size_t path_l)
{
	int retval;
	char *t_path = user_strdup_errno(p, path, path_l);
	if (!t_path)
		return -1;
	retval = do_rmdir(t_path);
	user_memdup_free(p, t_path);
	return retval;
}

intreg_t sys_gettimeofday(struct proc *p, int *buf)
{
	static spinlock_t gtod_lock = SPINLOCK_INITIALIZER;
	static int t0 = 0;

	spin_lock(&gtod_lock);
	if(t0 == 0)

#if (defined __CONFIG_APPSERVER__)
	t0 = ufe(time,0,0,0,0);
#else
	// Nanwan's birthday, bitches!!
	t0 = 1242129600;
#endif 
	spin_unlock(&gtod_lock);

	long long dt = read_tsc();
	int kbuf[2] = {t0+dt/system_timing.tsc_freq,
	    (dt%system_timing.tsc_freq)*1000000/system_timing.tsc_freq};

	return memcpy_to_user_errno(p,buf,kbuf,sizeof(kbuf));
}

#define SIZEOF_STRUCT_TERMIOS 60
intreg_t sys_tcgetattr(struct proc *p, int fd, void *termios_p)
{
	int* kbuf = kmalloc(SIZEOF_STRUCT_TERMIOS,0);
	int ret = ufe(tcgetattr,fd,PADDR(kbuf),0,0);
	if(ret != -1 && memcpy_to_user_errno(p,termios_p,kbuf,SIZEOF_STRUCT_TERMIOS))
		ret = -1;
	kfree(kbuf);
	return ret;
}

intreg_t sys_tcsetattr(struct proc *p, int fd, int optional_actions,
                       const void *termios_p)
{
	void* kbuf = user_memdup_errno(p,termios_p,SIZEOF_STRUCT_TERMIOS);
	if(kbuf == NULL)
		return -1;
	int ret = ufe(tcsetattr,fd,optional_actions,PADDR(kbuf),0);
	user_memdup_free(p,kbuf);
	return ret;
}

/* TODO: we don't have any notion of UIDs or GIDs yet, but don't let that stop a
 * process from thinking it can do these.  The other alternative is to have
 * glibc return 0 right away, though someone might want to do something with
 * these calls.  Someday. */
intreg_t sys_setuid(struct proc *p, uid_t uid)
{
	return 0;
}

intreg_t sys_setgid(struct proc *p, gid_t gid)
{
	return 0;
}

/************** Syscall Invokation **************/

const static struct sys_table_entry syscall_table[] = {
	[SYS_null] = {(syscall_t)sys_null, "null"},
	[SYS_block] = {(syscall_t)sys_block, "block"},
	[SYS_cache_buster] = {(syscall_t)sys_cache_buster, "buster"},
	[SYS_cache_invalidate] = {(syscall_t)sys_cache_invalidate, "wbinv"},
	[SYS_reboot] = {(syscall_t)reboot, "reboot!"},
	[SYS_cputs] = {(syscall_t)sys_cputs, "cputs"},
	[SYS_cgetc] = {(syscall_t)sys_cgetc, "cgetc"},
	[SYS_getcpuid] = {(syscall_t)sys_getcpuid, "getcpuid"},
	[SYS_getvcoreid] = {(syscall_t)sys_getvcoreid, "getvcoreid"},
	[SYS_getpid] = {(syscall_t)sys_getpid, "getpid"},
	[SYS_proc_create] = {(syscall_t)sys_proc_create, "proc_create"},
	[SYS_proc_run] = {(syscall_t)sys_proc_run, "proc_run"},
	[SYS_proc_destroy] = {(syscall_t)sys_proc_destroy, "proc_destroy"},
	[SYS_yield] = {(syscall_t)sys_proc_yield, "proc_yield"},
	[SYS_fork] = {(syscall_t)sys_fork, "fork"},
	[SYS_exec] = {(syscall_t)sys_exec, "exec"},
	[SYS_trywait] = {(syscall_t)sys_trywait, "trywait"},
	[SYS_mmap] = {(syscall_t)sys_mmap, "mmap"},
	[SYS_munmap] = {(syscall_t)sys_munmap, "munmap"},
	[SYS_mprotect] = {(syscall_t)sys_mprotect, "mprotect"},
	[SYS_shared_page_alloc] = {(syscall_t)sys_shared_page_alloc, "pa"},
	[SYS_shared_page_free] = {(syscall_t)sys_shared_page_free, "pf"},
	[SYS_resource_req] = {(syscall_t)sys_resource_req, "resource_req"},
	[SYS_notify] = {(syscall_t)sys_notify, "notify"},
	[SYS_self_notify] = {(syscall_t)sys_self_notify, "self_notify"},
	[SYS_halt_core] = {(syscall_t)sys_halt_core, "halt_core"},
#ifdef __CONFIG_SERIAL_IO__
	[SYS_serial_read] = {(syscall_t)sys_serial_read, "ser_read"},
	[SYS_serial_write] = {(syscall_t)sys_serial_write, "ser_write"},
#endif
#ifdef __CONFIG_NETWORKING__
	[SYS_eth_read] = {(syscall_t)sys_eth_read, "eth_read"},
	[SYS_eth_write] = {(syscall_t)sys_eth_write, "eth_write"},
	[SYS_eth_get_mac_addr] = {(syscall_t)sys_eth_get_mac_addr, "get_mac"},
	[SYS_eth_recv_check] = {(syscall_t)sys_eth_recv_check, "recv_check"},
#endif
#ifdef __CONFIG_ARSC_SERVER__
	[SYS_init_arsc] = {(syscall_t)sys_init_arsc, "init_arsc"},
#endif
	[SYS_read] = {(syscall_t)sys_read, "read"},
	[SYS_write] = {(syscall_t)sys_write, "write"},
	[SYS_open] = {(syscall_t)sys_open, "open"},
	[SYS_close] = {(syscall_t)sys_close, "close"},
	[SYS_fstat] = {(syscall_t)sys_fstat, "fstat"},
	[SYS_stat] = {(syscall_t)sys_stat, "stat"},
	[SYS_lstat] = {(syscall_t)sys_lstat, "lstat"},
	[SYS_fcntl] = {(syscall_t)sys_fcntl, "fcntl"},
	[SYS_access] = {(syscall_t)sys_access, "access"},
	[SYS_umask] = {(syscall_t)sys_umask, "umask"},
	[SYS_chmod] = {(syscall_t)sys_chmod, "chmod"},
	[SYS_lseek] = {(syscall_t)sys_lseek, "lseek"},
	[SYS_link] = {(syscall_t)sys_link, "link"},
	[SYS_unlink] = {(syscall_t)sys_unlink, "unlink"},
	[SYS_symlink] = {(syscall_t)sys_symlink, "symlink"},
	[SYS_readlink] = {(syscall_t)sys_readlink, "readlink"},
	[SYS_chdir] = {(syscall_t)sys_chdir, "chdir"},
	[SYS_getcwd] = {(syscall_t)sys_getcwd, "getcwd"},
	[SYS_mkdir] = {(syscall_t)sys_mkdir, "mkdri"},
	[SYS_rmdir] = {(syscall_t)sys_rmdir, "rmdir"},
	[SYS_gettimeofday] = {(syscall_t)sys_gettimeofday, "gettime"},
	[SYS_tcgetattr] = {(syscall_t)sys_tcgetattr, "tcgetattr"},
	[SYS_tcsetattr] = {(syscall_t)sys_tcsetattr, "tcsetattr"},
	[SYS_setuid] = {(syscall_t)sys_setuid, "setuid"},
	[SYS_setgid] = {(syscall_t)sys_setgid, "setgid"}
};

/* Executes the given syscall.
 *
 * Note tf is passed in, which points to the tf of the context on the kernel
 * stack.  If any syscall needs to block, it needs to save this info, as well as
 * any silly state.
 * 
 * This syscall function is used by both local syscall and arsc, and should
 * remain oblivious of the caller. */
intreg_t syscall(struct proc *p, uintreg_t sc_num, uintreg_t a0, uintreg_t a1,
                 uintreg_t a2, uintreg_t a3, uintreg_t a4, uintreg_t a5)
{
	const int max_syscall = sizeof(syscall_table)/sizeof(syscall_table[0]);

	uint32_t coreid, vcoreid;
	if (systrace_flags & SYSTRACE_ON) {
		if ((systrace_flags & SYSTRACE_ALLPROC) || (proc_is_traced(p))) {
			coreid = core_id();
			vcoreid = proc_get_vcoreid(p, coreid);
			if (systrace_flags & SYSTRACE_LOUD) {
				printk("[%16llu] Syscall %3d (%12s):(%08p, %08p, %08p, %08p, "
				       "%08p, %08p) proc: %d core: %d vcore: %d\n", read_tsc(),
				       sc_num, syscall_table[sc_num].name, a0, a1, a2, a3,
				       a4, a5, p->pid, coreid, vcoreid);
			} else {
				struct systrace_record *trace;
				unsigned int idx, new_idx;
				do {
					idx = systrace_bufidx;
					new_idx = (idx + 1) % systrace_bufsize;
				} while (!atomic_comp_swap(&systrace_bufidx, idx, new_idx));
				trace = &systrace_buffer[idx];
				trace->timestamp = read_tsc();
				trace->syscallno = sc_num;
				trace->arg0 = a0;
				trace->arg1 = a1;
				trace->arg2 = a2;
				trace->arg3 = a3;
				trace->arg4 = a4;
				trace->arg5 = a5;
				trace->pid = p->pid;
				trace->coreid = coreid;
				trace->vcoreid = vcoreid;
			}
		}
	}
	if (sc_num > max_syscall || syscall_table[sc_num].call == NULL)
		panic("Invalid syscall number %d for proc %x!", sc_num, p);

	return syscall_table[sc_num].call(p, a0, a1, a2, a3, a4, a5);
}

/* Execute the syscall on the local core */
void run_local_syscall(struct syscall *sysc)
{
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];

	/* TODO: (UMEM) assert / pin the memory for the sysc */
	user_mem_assert(pcpui->cur_proc, sysc, sizeof(struct syscall), PTE_USER_RW);
	pcpui->cur_sysc = sysc;			/* let the core know which sysc it is */
	sysc->retval = syscall(pcpui->cur_proc, sysc->num, sysc->arg0, sysc->arg1,
	                       sysc->arg2, sysc->arg3, sysc->arg4, sysc->arg5);
	/* Atomically turn on the SC_DONE flag.  Need the atomics since we're racing
	 * with userspace for the event_queue registration. */
	atomic_or(&sysc->flags, SC_DONE); 
	signal_syscall(sysc, pcpui->cur_proc);
	/* Can unpin (UMEM) at this point */
	pcpui->cur_sysc = 0;	/* no longer working on sysc */
}

/* A process can trap and call this function, which will set up the core to
 * handle all the syscalls.  a.k.a. "sys_debutante(needs, wants)".  If there is
 * at least one, it will run it directly. */
void prep_syscalls(struct proc *p, struct syscall *sysc, unsigned int nr_syscs)
{
	int retval;
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	if (!nr_syscs)
		return;
	/* For all after the first call, send ourselves a KMSG (TODO). */
	if (nr_syscs != 1)
		warn("Only one supported (Debutante calls: %d)\n", nr_syscs);
	/* Call the first one directly.  (we already checked to make sure there is
	 * 1) */
	run_local_syscall(sysc);
}

/* Call this when something happens on the syscall where userspace might want to
 * get signaled.  Passing p, since the caller should know who the syscall
 * belongs to (probably is current). */
void signal_syscall(struct syscall *sysc, struct proc *p)
{
	struct event_queue *ev_q;
	struct event_msg local_msg;
	/* User sets the ev_q then atomically sets the flag (races with SC_DONE) */
	if (atomic_read(&sysc->flags) & SC_UEVENT) {
		rmb();
		ev_q = sysc->ev_q;
		if (ev_q) {
			memset(&local_msg, 0, sizeof(struct event_msg));
			local_msg.ev_type = EV_SYSCALL;
			local_msg.ev_arg3 = sysc;
			send_event(p, ev_q, &local_msg, 0);
		}
	}
}

/* Syscall tracing */
static void __init_systrace(void)
{
	systrace_buffer = kmalloc(MAX_SYSTRACES*sizeof(struct systrace_record), 0);
	if (!systrace_buffer)
		panic("Unable to alloc a trace buffer\n");
	systrace_bufidx = 0;
	systrace_bufsize = MAX_SYSTRACES;
	/* Note we never free the buffer - it's around forever.  Feel free to change
	 * this if you want to change the size or something dynamically. */
}

/* If you call this while it is running, it will change the mode */
void systrace_start(bool silent)
{
	static bool init = FALSE;
	spin_lock_irqsave(&systrace_lock);
	if (!init) {
		__init_systrace();
		init = TRUE;
	}
	systrace_flags = silent ? SYSTRACE_ON : SYSTRACE_ON | SYSTRACE_LOUD; 
	spin_unlock_irqsave(&systrace_lock);
}

int systrace_reg(bool all, struct proc *p)
{
	int retval = 0;
	spin_lock_irqsave(&systrace_lock);
	if (all) {
		printk("Tracing syscalls for all processes\n");
		systrace_flags |= SYSTRACE_ALLPROC;
		retval = 0;
	} else {
		for (int i = 0; i < MAX_NUM_TRACED; i++) {
			if (!systrace_procs[i]) {
				printk("Tracing syscalls for process %d\n", p->pid);
				systrace_procs[i] = p;
				retval = 0;
				break;
			}
		}
	}
	spin_unlock_irqsave(&systrace_lock);
	return retval;
}

void systrace_stop(void)
{
	spin_lock_irqsave(&systrace_lock);
	systrace_flags = 0;
	for (int i = 0; i < MAX_NUM_TRACED; i++)
		systrace_procs[i] = 0;
	spin_unlock_irqsave(&systrace_lock);
}

/* If you registered a process specifically, then you need to dereg it
 * specifically.  Or just fully stop, which will do it for all. */
int systrace_dereg(bool all, struct proc *p)
{
	spin_lock_irqsave(&systrace_lock);
	if (all) {
		printk("No longer tracing syscalls for all processes.\n");
		systrace_flags &= ~SYSTRACE_ALLPROC;
	} else {
		for (int i = 0; i < MAX_NUM_TRACED; i++) {
			if (systrace_procs[i] == p) {
				systrace_procs[i] = 0;
				printk("No longer tracing syscalls for process %d\n", p->pid);
			}
		}
	}
	spin_unlock_irqsave(&systrace_lock);
	return 0;
}

/* Regardless of locking, someone could be writing into the buffer */
void systrace_print(bool all, struct proc *p)
{
	spin_lock_irqsave(&systrace_lock);
	/* if you want to be clever, you could make this start from the earliest
	 * timestamp and loop around.  Careful of concurrent writes. */
	for (int i = 0; i < systrace_bufsize; i++)
		if (systrace_buffer[i].timestamp)
			printk("[%16llu] Syscall %3d (%12s):(%08p, %08p, %08p, %08p, %08p,"
			       "%08p) proc: %d core: %d vcore: %d\n",
			       systrace_buffer[i].timestamp,
			       systrace_buffer[i].syscallno,
			       syscall_table[systrace_buffer[i].syscallno].name,
			       systrace_buffer[i].arg0,
			       systrace_buffer[i].arg1,
			       systrace_buffer[i].arg2,
			       systrace_buffer[i].arg3,
			       systrace_buffer[i].arg4,
			       systrace_buffer[i].arg5,
			       systrace_buffer[i].pid,
			       systrace_buffer[i].coreid,
			       systrace_buffer[i].vcoreid);
	spin_unlock_irqsave(&systrace_lock);
}

void systrace_clear_buffer(void)
{
	spin_lock_irqsave(&systrace_lock);
	memset(systrace_buffer, 0, sizeof(struct systrace_record) * MAX_SYSTRACES);
	spin_unlock_irqsave(&systrace_lock);
}
