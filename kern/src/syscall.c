/* See COPYRIGHT for copyright information. */

#ifdef __SHARC__
#pragma nosharc
#endif

#include <ros/common.h>
#include <ros/notification.h>
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
#include <arch/bitmask.h>
#include <kfs.h> // eventually replace this with vfs.h


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

/************** Utility Syscalls **************/

static int sys_null(void)
{
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

// Print a string to the system console.
// The string is exactly 'len' characters long.
// Destroys the environment on memory errors.
static ssize_t sys_cputs(env_t* e, const char *DANGEROUS s, size_t len)
{
	// Check that the user has permission to read memory [s, s+len).
	// Destroy the environment if not.
	char *COUNT(len) _s = user_mem_assert(e, s, len, PTE_USER_RO);

	// Print the string supplied by the user.
	printk("%.*s", len, _s);
	return (ssize_t)len;
}

// Read a character from the system console.
// Returns the character.
static uint16_t sys_cgetc(env_t* e)
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

	/* Copy in the path.  Consider putting an upper bound. */
	t_path = kmalloc(path_l, 0);
	if (!t_path) {
		set_errno(current_tf, ENOMEM);
		return -1;
	}
	if (memcpy_from_user(p, t_path, path, path_l)) {
		kfree(t_path);
		set_errno(current_tf, EINVAL);
		return -1;
	}
	program = path_to_file(t_path);
	kfree(t_path);
	if (!program)
		return -1;			/* presumably, errno is already set */
	new_p = proc_create(program, 0, 0);
	/* Set the argument stuff needed by glibc */
	if (memcpy_from_user(p, new_p->procinfo->argp, pi->argp, sizeof(pi->argp))){
		atomic_dec(&program->f_refcnt);	/* TODO: REF */
		proc_destroy(new_p);
		set_errno(current_tf, EINVAL);
		return -1;
	}
	if (memcpy_from_user(p, new_p->procinfo->argbuf, pi->argbuf,
	                     sizeof(pi->argbuf))) {
		atomic_dec(&program->f_refcnt);	/* TODO: REF */
		proc_destroy(new_p);
		set_errno(current_tf, EINVAL);
		return -1;
	}
	pid = new_p->pid;
	proc_decref(new_p, 1);	/* give up the reference created in proc_create() */
	atomic_dec(&program->f_refcnt);		/* TODO: REF / KREF */
	return pid;
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
		proc_decref(target, 1);
		retval = -EPERM;
	} else if (target->state != PROC_CREATED) {
		proc_decref(target, 1);
		retval = -EINVAL;
	} else {
		__proc_set_state(target, PROC_RUNNABLE_S);
		schedule_proc(target);
	}
	spin_unlock(&p->proc_lock);
	proc_decref(target, 1);
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
		set_errno(current_tf, ESRCH);
		return -1;
	}
	if (!proc_controls(p, p_to_die)) {
		proc_decref(p_to_die, 1);
		set_errno(current_tf, EPERM);
		return -1;
	}
	if (p_to_die == p) {
		// syscall code and pid2proc both have edible references, only need 1.
		p->exitcode = exitcode;
		proc_decref(p, 1);
		printd("[PID %d] proc exiting gracefully (code %d)\n", p->pid,exitcode);
	} else {
		printd("[%d] destroying proc %d\n", p->pid, p_to_die->pid);
	}
	proc_destroy(p_to_die);
	proc_decref(p_to_die, 1);
	return ESUCCESS;
}

static int sys_proc_yield(struct proc *p, bool being_nice)
{
	proc_yield(p, being_nice);
	return 0;
}

static ssize_t sys_fork(env_t* e)
{
	// TODO: right now we only support fork for single-core processes
	if(e->state != PROC_RUNNING_S)
	{
		set_errno(current_tf,EINVAL);
		return -1;
	}

	env_t* env;
	assert(!proc_alloc(&env, current));
	assert(env != NULL);

	env->heap_top = e->heap_top;
	env->ppid = e->pid;
	env->env_tf = *current_tf;

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
		proc_decref(env,2);
		set_errno(current_tf,ENOMEM);
		return -1;
	}

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
 * glibc's sysdeps/ros/execve.c */
static int sys_exec(struct proc *p, char *path, size_t path_l,
                    struct procinfo *pi)
{
	int ret = -1;
	char *t_path;
	struct file *program;

	/* We probably want it to never be allowed to exec if it ever was _M */
	if(p->state != PROC_RUNNING_S)
		return -1;
	/* Copy in the path.  Consider putting an upper bound. */
	t_path = kmalloc(path_l, 0);
	if (!t_path) {
		set_errno(current_tf, ENOMEM);
		return -1;
	}
	if (memcpy_from_user(p, t_path, path, path_l)) {
		kfree(t_path);
		set_errno(current_tf, EINVAL);
		return -1;
	}
	program = path_to_file(t_path);
	kfree(t_path);
	if (!program)
		return -1;			/* presumably, errno is already set */
	/* Set the argument stuff needed by glibc */
	if (memcpy_from_user(p, p->procinfo->argp, pi->argp, sizeof(pi->argp))) {
		atomic_dec(&program->f_refcnt);	/* TODO: REF */
		set_errno(current_tf, EINVAL);
		return -1;
	}
	if (memcpy_from_user(p, p->procinfo->argbuf, pi->argbuf,
	                     sizeof(pi->argbuf))) {
		atomic_dec(&program->f_refcnt);	/* TODO: REF */
		set_errno(current_tf, EINVAL);
		return -1;
	}
	/* This is the point of no return for the process. */
	/* TODO: issues with this: Need to also assert there are no outstanding
	 * users of the sysrings.  the ldt page will get freed shortly, so that's
	 * okay.  Potentially issues with the nm and vcpd if we were in _M before
	 * and someone is trying to notify. */
	memset(p->procdata, 0, sizeof(procdata_t));
	env_user_mem_free(p, 0, UMAPTOP);
	if (load_elf(p, program)) {
		proc_destroy(p);
		smp_idle();		/* syscall can't return on failure now */
	}
	printk("[PID %d] exec %s\n", p->pid, file_name(program));
	atomic_dec(&program->f_refcnt);		/* TODO: (REF) / KREF */
	*current_tf = p->env_tf;
	return 0;
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
				set_errno(current_tf,0);
				ret = -1;
			}
		}
		else // not a child of the calling process
		{
			set_errno(current_tf,1);
			ret = -1;
		}

		// if the wait succeeded, decref twice
		proc_decref(p,1 + (ret == 0));
		return ret;
	}

	set_errno(current_tf,1);
	return -1;
}

/************** Memory Management Syscalls **************/

static void *sys_mmap(struct proc* p, uintreg_t a1, uintreg_t a2, uintreg_t a3,
                      uintreg_t* a456)
{
	uintreg_t _a456[3];
	if(memcpy_from_user(p,_a456,a456,3*sizeof(uintreg_t)))
		sys_proc_destroy(p,p->pid,-1);
	return mmap(p,a1,a2,a3,_a456[0],_a456[1],_a456[2]);
}

static intreg_t sys_mprotect(struct proc* p, void* addr, size_t len, int prot)
{
	return mprotect(p, (uintptr_t)addr, len, prot);
}

static intreg_t sys_munmap(struct proc* p, void* addr, size_t len)
{
	return munmap(p, (uintptr_t)addr, len);
}

static void* sys_brk(struct proc *p, void* addr) {
	ssize_t range;

	// TODO: remove sys_brk
	printk("[kernel] don't use brk, unsupported and will be removed soon.\n");

	spin_lock(&p->proc_lock);

	if((addr < p->procinfo->heap_bottom) || (addr >= (void*)BRK_END))
		goto out;

	uintptr_t real_heap_top = ROUNDUP((uintptr_t)p->heap_top,PGSIZE);
	uintptr_t real_new_heap_top = ROUNDUP((uintptr_t)addr,PGSIZE);
	range = real_new_heap_top - real_heap_top;

	if (range > 0) {
		if(__do_mmap(p, real_heap_top, range, PROT_READ | PROT_WRITE,
		             MAP_FIXED | MAP_ANONYMOUS, NULL, 0) == MAP_FAILED)
			goto out;
	}
	else if (range < 0) {
		if(__do_munmap(p, real_new_heap_top, -range))
			goto out;
	}
	p->heap_top = addr;

out:
	spin_unlock(&p->proc_lock);
	return p->heap_top;
}

static ssize_t sys_shared_page_alloc(env_t* p1,
                                     void**DANGEROUS _addr, pid_t p2_id,
                                     int p1_flags, int p2_flags
                                    )
{
	/* When we remove/change this, also get rid of page_insert_in_range() */
	printk("[kernel] the current shared page alloc is deprecated.\n");
	//if (!VALID_USER_PERMS(p1_flags)) return -EPERM;
	//if (!VALID_USER_PERMS(p2_flags)) return -EPERM;

	void * COUNT(1) * COUNT(1) addr = user_mem_assert(p1, _addr, sizeof(void *),
                                                      PTE_USER_RW);
	struct proc *p2 = pid2proc(p2_id);
	if (!p2)
		return -EBADPROC;

	page_t* page;
	error_t e = upage_alloc(p1, &page,1);
	if (e < 0) {
		proc_decref(p2, 1);
		return e;
	}

	void* p2_addr = page_insert_in_range(p2->env_pgdir, page,
	                (void*SNT)UTEXT, (void*SNT)UTOP, p2_flags);
	if (p2_addr == NULL) {
		page_free(page);
		proc_decref(p2, 1);
		return -EFAIL;
	}

	void* p1_addr = page_insert_in_range(p1->env_pgdir, page,
	                (void*SNT)UTEXT, (void*SNT)UTOP, p1_flags);
	if(p1_addr == NULL) {
		page_remove(p2->env_pgdir, p2_addr);
		page_free(page);
		proc_decref(p2, 1);
		return -EFAIL;
	}
	*addr = p1_addr;
	proc_decref(p2, 1);
	return ESUCCESS;
}

static int sys_shared_page_free(env_t* p1, void*DANGEROUS addr, pid_t p2)
{
	return -1;
}


/* sys_resource_req(): called directly from dispatch table. */

/* Will notify the target on the given vcore, if the caller controls the target.
 * Will honor the target's wanted/vcoreid.  u_ne can be NULL. */
static int sys_notify(struct proc *p, int target_pid, unsigned int notif,
                      struct notif_event *u_ne)
{
	struct notif_event local_ne;
	struct proc *target = pid2proc(target_pid);

	if (!target) {
		set_errno(current_tf, EBADPROC);
		return -1;
	}
	if (!proc_controls(p, target)) {
		proc_decref(target, 1);
		set_errno(current_tf, EPERM);
		return -1;
	}
	/* if the user provided a notif_event, copy it in and use that */
	if (u_ne) {
		if (memcpy_from_user(p, &local_ne, u_ne, sizeof(struct notif_event))) {
			proc_decref(target, 1);
			set_errno(current_tf, EINVAL);
			return -1;
		}
		proc_notify(target, local_ne.ne_type, &local_ne);
	} else {
		proc_notify(target, notif, 0);
	}
	proc_decref(target, 1);
	return 0;
}

/* Will notify the calling process on the given vcore, independently of WANTED
 * or advertised vcoreid.  If you change the parameters, change pop_ros_tf() */
static int sys_self_notify(struct proc *p, uint32_t vcoreid, unsigned int notif,
                           struct notif_event *u_ne)
{
	struct notif_event local_ne;

	printd("[kernel] received self notify for vcoreid %d, notif %d, ne %08p\n",
	       vcoreid, notif, u_ne);
	/* if the user provided a notif_event, copy it in and use that */
	if (u_ne) {
		if (memcpy_from_user(p, &local_ne, u_ne, sizeof(struct notif_event))) {
			set_errno(current_tf, EINVAL);
			return -1;
		}
		do_notify(p, vcoreid, local_ne.ne_type, &local_ne);
	} else {
		do_notify(p, vcoreid, notif, 0);
	}
	return 0;
}

/* This will set a local timer for usec, then shut down the core */
static int sys_halt_core(struct proc *p, unsigned int usec)
{
	/* TODO: ought to check and see if a timer was already active, etc, esp so
	 * userspace can't turn off timers.  also note we will also call whatever
	 * timer_interrupt() will do, though all we care about is just
	 * self_ipi/interrupting. */
	set_core_timer(usec);
	cpu_halt();

	return 0;
}

/************** Platform Specific Syscalls **************/

//Read a buffer over the serial port
static ssize_t sys_serial_read(env_t* e, char *DANGEROUS _buf, size_t len)
{
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

// Syscalls below here are serviced by the appserver for now.
#define ufe(which,a0,a1,a2,a3) \
	frontend_syscall_errno(p,APPSERVER_SYSCALL_##which,\
	                   (int)(a0),(int)(a1),(int)(a2),(int)(a3))

intreg_t sys_write(struct proc* p, int fd, const void* buf, int len)
{
	int ret = 0;
	void* kbuf = user_memdup_errno(p,buf,len);
	if(kbuf == NULL)
		return -1;
#ifndef __CONFIG_APPSERVER__
	/* Catch a common usage of stderr */
	if (fd == 2) {
		((char*)kbuf)[len-1] = 0;
		printk("[stderr]: %s\n", kbuf);
		ret = len;
	} else { // but warn/panic otherwise in ufe()
		ret = ufe(write, fd, PADDR(kbuf), len, 0);
	}
#else
	ret = ufe(write, fd, PADDR(kbuf), len, 0);
#endif
	user_memdup_free(p,kbuf);
	return ret;
}

intreg_t sys_read(struct proc* p, int fd, void* buf, int len)
{
	void* kbuf = kmalloc_errno(len);
	if(kbuf == NULL)
		return -1;
	int ret = ufe(read,fd,PADDR(kbuf),len,0);
	if(ret != -1 && memcpy_to_user_errno(p,buf,kbuf,len))
		ret = -1;
	user_memdup_free(p,kbuf);
	return ret;
}

intreg_t sys_pwrite(struct proc* p, int fd, const void* buf, int len, int offset)
{
	void* kbuf = user_memdup_errno(p,buf,len);
	if(kbuf == NULL)
		return -1;
	int ret = ufe(pwrite,fd,PADDR(kbuf),len,offset);
	user_memdup_free(p,kbuf);
	return ret;
}

intreg_t sys_pread(struct proc* p, int fd, void* buf, int len, int offset)
{
	void* kbuf = kmalloc_errno(len);
	if(kbuf == NULL)
		return -1;
	int ret = ufe(pread,fd,PADDR(kbuf),len,offset);
	if(ret != -1 && memcpy_to_user_errno(p,buf,kbuf,len))
		ret = -1;
	user_memdup_free(p,kbuf);
	return ret;
}

intreg_t sys_open(struct proc* p, const char* path, int oflag, int mode)
{
	printd("File Open, p: %p, path: %s, oflag: %d, mode: 0x%x\n", p, path, oflag, mode);
	char* fn = user_strdup_errno(p,path,PGSIZE);
	if(fn == NULL) {
		printd("File Open, user_strdup_errno failed\n");
		return -1;
	}
	printd("File Open, About to open\n");
	int ret = ufe(open,PADDR(fn),oflag,mode,0);
	printd("File Open, res=%d\n", ret);
	user_memdup_free(p,fn);
	return ret;
}
intreg_t sys_close(struct proc* p, int fd)
{
	return ufe(close,fd,0,0,0);
}

#define NEWLIB_STAT_SIZE 64
intreg_t sys_fstat(struct proc* p, int fd, void* buf)
{
	int *kbuf = kmalloc(NEWLIB_STAT_SIZE, 0);
	int ret = ufe(fstat,fd,PADDR(kbuf),0,0);
	if(ret != -1 && memcpy_to_user_errno(p,buf,kbuf,NEWLIB_STAT_SIZE))
		ret = -1;
	kfree(kbuf);
	return ret;
}

intreg_t sys_stat(struct proc* p, const char* path, void* buf)
{
	char* fn = user_strdup_errno(p,path,PGSIZE);
	if(fn == NULL)
		return -1;

	int *kbuf = kmalloc(NEWLIB_STAT_SIZE, 0);
	int ret = ufe(stat,PADDR(fn),PADDR(kbuf),0,0);
	if(ret != -1 && memcpy_to_user_errno(p,buf,kbuf,NEWLIB_STAT_SIZE))
		ret = -1;

	user_memdup_free(p,fn);
	kfree(kbuf);
	return ret;
}

intreg_t sys_lstat(struct proc* p, const char* path, void* buf)
{
	char* fn = user_strdup_errno(p,path,PGSIZE);
	if(fn == NULL)
		return -1;

	int *kbuf = kmalloc(NEWLIB_STAT_SIZE, 0);
	int ret = ufe(lstat,PADDR(fn),PADDR(kbuf),0,0);
	if(ret != -1 && memcpy_to_user_errno(p,buf,kbuf,NEWLIB_STAT_SIZE))
		ret = -1;

	user_memdup_free(p,fn);
	kfree(kbuf);
	return ret;
}

intreg_t sys_fcntl(struct proc* p, int fd, int cmd, int arg)
{
	return ufe(fcntl,fd,cmd,arg,0);
}

intreg_t sys_access(struct proc* p, const char* path, int type)
{
	char* fn = user_strdup_errno(p,path,PGSIZE);
	if(fn == NULL)
		return -1;
	int ret = ufe(access,PADDR(fn),type,0,0);
	user_memdup_free(p,fn);
	return ret;
}

intreg_t sys_umask(struct proc* p, int mask)
{
	return ufe(umask,mask,0,0,0);
}

intreg_t sys_chmod(struct proc* p, const char* path, int mode)
{
	char* fn = user_strdup_errno(p,path,PGSIZE);
	if(fn == NULL)
		return -1;
	int ret = ufe(chmod,PADDR(fn),mode,0,0);
	user_memdup_free(p,fn);
	return ret;
}

intreg_t sys_lseek(struct proc* p, int fd, int offset, int whence)
{
	return ufe(lseek,fd,offset,whence,0);
}

intreg_t sys_link(struct proc* p, const char* _old, const char* _new)
{
	char* oldpath = user_strdup_errno(p,_old,PGSIZE);
	if(oldpath == NULL)
		return -1;

	char* newpath = user_strdup_errno(p,_new,PGSIZE);
	if(newpath == NULL)
	{
		user_memdup_free(p,oldpath);
		return -1;
	}

	int ret = ufe(link,PADDR(oldpath),PADDR(newpath),0,0);
	user_memdup_free(p,oldpath);
	user_memdup_free(p,newpath);
	return ret;
}

intreg_t sys_unlink(struct proc* p, const char* path)
{
	char* fn = user_strdup_errno(p,path,PGSIZE);
	if(fn == NULL)
		return -1;
	int ret = ufe(unlink,PADDR(fn),0,0,0);
	user_memdup_free(p,fn);
	return ret;
}

intreg_t sys_chdir(struct proc* p, const char* path)
{
	char* fn = user_strdup_errno(p,path,PGSIZE);
	if(fn == NULL)
		return -1;
	int ret = ufe(chdir,PADDR(fn),0,0,0);
	user_memdup_free(p,fn);
	return ret;
}

intreg_t sys_getcwd(struct proc* p, char* pwd, int size)
{
	void* kbuf = kmalloc_errno(size);
	if(kbuf == NULL)
		return -1;
	int ret = ufe(read,PADDR(kbuf),size,0,0);
	if(ret != -1 && memcpy_to_user_errno(p,pwd,kbuf,strnlen(kbuf,size)))
		ret = -1;
	user_memdup_free(p,kbuf);
	return ret;
}

intreg_t sys_gettimeofday(struct proc* p, int* buf)
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
intreg_t sys_tcgetattr(struct proc* p, int fd, void* termios_p)
{
	int* kbuf = kmalloc(SIZEOF_STRUCT_TERMIOS,0);
	int ret = ufe(tcgetattr,fd,PADDR(kbuf),0,0);
	if(ret != -1 && memcpy_to_user_errno(p,termios_p,kbuf,SIZEOF_STRUCT_TERMIOS))
		ret = -1;
	kfree(kbuf);
	return ret;
}

intreg_t sys_tcsetattr(struct proc* p, int fd, int optional_actions, const void* termios_p)
{
	void* kbuf = user_memdup_errno(p,termios_p,SIZEOF_STRUCT_TERMIOS);
	if(kbuf == NULL)
		return -1;
	int ret = ufe(tcsetattr,fd,optional_actions,PADDR(kbuf),0);
	user_memdup_free(p,kbuf);
	return ret;
}

/************** Syscall Invokation **************/

/* Executes the given syscall.
 *
 * Note tf is passed in, which points to the tf of the context on the kernel
 * stack.  If any syscall needs to block, it needs to save this info, as well as
 * any silly state.
 *
 * TODO: Build a dispatch table instead of switching on the syscallno
 * Dispatches to the correct kernel function, passing the arguments.
 */
intreg_t syscall(struct proc *p, uintreg_t syscallno, uintreg_t a1,
                 uintreg_t a2, uintreg_t a3, uintreg_t a4, uintreg_t a5)
{
	// Initialize the return value and error code returned to 0
	proc_set_syscall_retval(current_tf, 0);
	set_errno(current_tf,0);

	typedef intreg_t (*syscall_t)(struct proc*,uintreg_t,uintreg_t,
	                              uintreg_t,uintreg_t,uintreg_t);

	const static syscall_t syscall_table[] = {
		[SYS_null] = (syscall_t)sys_null,
		[SYS_cache_buster] = (syscall_t)sys_cache_buster,
		[SYS_cache_invalidate] = (syscall_t)sys_cache_invalidate,
		[SYS_reboot] = (syscall_t)reboot,
		[SYS_cputs] = (syscall_t)sys_cputs,
		[SYS_cgetc] = (syscall_t)sys_cgetc,
		[SYS_getcpuid] = (syscall_t)sys_getcpuid,
		[SYS_getvcoreid] = (syscall_t)sys_getvcoreid,
		[SYS_getpid] = (syscall_t)sys_getpid,
		[SYS_proc_create] = (syscall_t)sys_proc_create,
		[SYS_proc_run] = (syscall_t)sys_proc_run,
		[SYS_proc_destroy] = (syscall_t)sys_proc_destroy,
		[SYS_yield] = (syscall_t)sys_proc_yield,
		[SYS_fork] = (syscall_t)sys_fork,
		[SYS_exec] = (syscall_t)sys_exec,
		[SYS_trywait] = (syscall_t)sys_trywait,
		[SYS_mmap] = (syscall_t)sys_mmap,
		[SYS_munmap] = (syscall_t)sys_munmap,
		[SYS_mprotect] = (syscall_t)sys_mprotect,
		[SYS_brk] = (syscall_t)sys_brk,
		[SYS_shared_page_alloc] = (syscall_t)sys_shared_page_alloc,
		[SYS_shared_page_free] = (syscall_t)sys_shared_page_free,
		[SYS_resource_req] = (syscall_t)resource_req,
		[SYS_notify] = (syscall_t)sys_notify,
		[SYS_self_notify] = (syscall_t)sys_self_notify,
		[SYS_halt_core] = (syscall_t)sys_halt_core,
	#ifdef __CONFIG_SERIAL_IO__
		[SYS_serial_read] = (syscall_t)sys_serial_read,
		[SYS_serial_write] = (syscall_t)sys_serial_write,
	#endif
	#ifdef __CONFIG_NETWORKING__
		[SYS_eth_read] = (syscall_t)sys_eth_read,
		[SYS_eth_write] = (syscall_t)sys_eth_write,
		[SYS_eth_get_mac_addr] = (syscall_t)sys_eth_get_mac_addr,
		[SYS_eth_recv_check] = (syscall_t)sys_eth_recv_check,
	#endif
		// Syscalls serviced by the appserver for now.
		[SYS_read] = (syscall_t)sys_read,
		[SYS_write] = (syscall_t)sys_write,
		[SYS_open] = (syscall_t)sys_open,
		[SYS_close] = (syscall_t)sys_close,
		[SYS_fstat] = (syscall_t)sys_fstat,
		[SYS_stat] = (syscall_t)sys_stat,
		[SYS_lstat] = (syscall_t)sys_lstat,
		[SYS_fcntl] = (syscall_t)sys_fcntl,
		[SYS_access] = (syscall_t)sys_access,
		[SYS_umask] = (syscall_t)sys_umask,
		[SYS_chmod] = (syscall_t)sys_chmod,
		[SYS_lseek] = (syscall_t)sys_lseek,
		[SYS_link] = (syscall_t)sys_link,
		[SYS_unlink] = (syscall_t)sys_unlink,
		[SYS_chdir] = (syscall_t)sys_chdir,
		[SYS_getcwd] = (syscall_t)sys_getcwd,
		[SYS_gettimeofday] = (syscall_t)sys_gettimeofday,
		[SYS_tcgetattr] = (syscall_t)sys_tcgetattr,
		[SYS_tcsetattr] = (syscall_t)sys_tcsetattr
	};

	const int max_syscall = sizeof(syscall_table)/sizeof(syscall_table[0]);

	uint32_t coreid, vcoreid;
	if (systrace_flags & SYSTRACE_ON) {
		if ((systrace_flags & SYSTRACE_ALLPROC) || (proc_is_traced(p))) {
			coreid = core_id();
			vcoreid = proc_get_vcoreid(p, core_id());
			if (systrace_flags & SYSTRACE_LOUD) {
				printk("[%16llu] Syscall %d for proc %d on core %d, vcore %d\n",
				       read_tsc(), syscallno, p->pid, coreid, vcoreid);
			} else {
				struct systrace_record *trace;
				unsigned int idx, new_idx;
				do {
					idx = systrace_bufidx;
					new_idx = (idx + 1) % systrace_bufsize;
				} while (!atomic_comp_swap(&systrace_bufidx, idx, new_idx));
				trace = &systrace_buffer[idx];
				trace->timestamp = read_tsc();
				trace->syscallno = syscallno;
				trace->pid = p->pid;
				trace->coreid = coreid;
				trace->vcoreid = vcoreid;
			}
		}
	}
	//printk("Incoming syscall on core: %d number: %d\n    a1: %x\n   "
	//       " a2: %x\n    a3: %x\n    a4: %x\n    a5: %x\n", core_id(),
	//       syscallno, a1, a2, a3, a4, a5);

	if(syscallno > max_syscall || syscall_table[syscallno] == NULL)
		panic("Invalid syscall number %d for proc %x!", syscallno, *p);

	return syscall_table[syscallno](p,a1,a2,a3,a4,a5);
}

intreg_t syscall_async(struct proc *p, syscall_req_t *call)
{
	return syscall(p, call->num, call->args[0], call->args[1],
	               call->args[2], call->args[3], call->args[4]);
}

/* You should already have a refcnt'd ref to p before calling this */
intreg_t process_generic_syscalls(struct proc *p, size_t max)
{
	size_t count = 0;
	syscall_back_ring_t* sysbr = &p->syscallbackring;

	/* make sure the proc is still alive, and keep it from dying from under us
	 * incref will return ESUCCESS on success.  This might need some thought
	 * regarding when the incref should have happened (like by whoever passed us
	 * the *p). */
	// TODO: ought to be unnecessary, if you called this right, kept here for
	// now in case anyone actually uses the ARSCs.
	proc_incref(p, 1);

	// max is the most we'll process.  max = 0 means do as many as possible
	while (RING_HAS_UNCONSUMED_REQUESTS(sysbr) && ((!max)||(count < max)) ) {
		if (!count) {
			// ASSUME: one queue per process
			// only switch cr3 for the very first request for this queue
			// need to switch to the right context, so we can handle the user pointer
			// that points to a data payload of the syscall
			lcr3(p->env_cr3);
		}
		count++;
		//printk("DEBUG PRE: sring->req_prod: %d, sring->rsp_prod: %d\n",
		//	   sysbr->sring->req_prod, sysbr->sring->rsp_prod);
		// might want to think about 0-ing this out, if we aren't
		// going to explicitly fill in all fields
		syscall_rsp_t rsp;
		// this assumes we get our answer immediately for the syscall.
		syscall_req_t* req = RING_GET_REQUEST(sysbr, ++(sysbr->req_cons));
		rsp.retval = syscall_async(p, req);
		// write response into the slot it came from
		memcpy(req, &rsp, sizeof(syscall_rsp_t));
		// update our counter for what we've produced (assumes we went in order!)
		(sysbr->rsp_prod_pvt)++;
		RING_PUSH_RESPONSES(sysbr);
		//printk("DEBUG POST: sring->req_prod: %d, sring->rsp_prod: %d\n",
		//	   sysbr->sring->req_prod, sysbr->sring->rsp_prod);
	}
	// load sane page tables (and don't rely on decref to do it for you).
	lcr3(boot_cr3);
	proc_decref(p, 1);
	return (intreg_t)count;
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
			printk("[%16llu] Syscall %d for proc %d on core %d, vcore %d\n",
			       systrace_buffer[i].timestamp,
			       systrace_buffer[i].syscallno,
			       systrace_buffer[i].pid,
			       systrace_buffer[i].coreid,
			       systrace_buffer[i].vcoreid);
	spin_unlock_irqsave(&systrace_lock);
}

void systrace_clear_buffer(void)
{
	spin_lock_irqsave(&systrace_lock);
	memset(systrace_buffer, 0, sizeof(struct systrace_record)*MAX_NUM_TRACED);
	spin_unlock_irqsave(&systrace_lock);
}
