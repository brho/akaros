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
#include <ros/error.h>

#include <string.h>
#include <assert.h>
#include <process.h>
#include <schedule.h>
#include <pmap.h>
#include <mm.h>
#include <trap.h>
#include <syscall.h>
#include <kmalloc.h>
#include <stdio.h>
#include <resource.h>
#include <colored_caches.h>
#include <arch/bitmask.h>
#include <kfs.h> // eventually replace this with vfs.h

#ifdef __sparc_v8__
#include <arch/frontend.h>
#endif 

#ifdef __NETWORK__
#include <arch/nic_common.h>
extern char *CT(PACKET_HEADER_SIZE + len) (*packet_wrap)(const char *CT(len) data, size_t len);
extern int (*send_frame)(const char *CT(len) data, size_t len);
#endif

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

// TODO: Temporary hack until thread-local storage is implemented on i386
static size_t sys_getvcoreid(env_t* e)
{
	if(e->state == PROC_RUNNING_S)
		return 0;

	size_t i;
	for(i = 0; i < e->num_vcores; i++)
		if(core_id() == e->vcoremap[i])
			return i;

	panic("virtual core id not found in sys_getvcoreid()!");
}

/************** Process management syscalls **************/

/* Returns the calling process's pid */
static pid_t sys_getpid(struct proc *p)
{
	return p->pid;
}

/*
 * Creates a process found at the user string 'path'.  Currently uses KFS.
 * Not runnable by default, so it needs it's status to be changed so that the
 * next call to schedule() will try to run it.
 * TODO: once we have a decent VFS, consider splitting this up
 * and once there's an mmap, can have most of this in process.c
 */
static int sys_proc_create(struct proc *p, const char *DANGEROUS path)
{
	#define MAX_PATH_LEN 256 // totally arbitrary
	int pid = 0;
	char tpath[MAX_PATH_LEN];
	/*
	 * There's a bunch of issues with reading in the path, which we'll
	 * need to sort properly in the VFS.  Main concerns are TOCTOU (copy-in),
	 * whether or not it's a big deal that the pointer could be into kernel
	 * space, and resolving both of these without knowing the length of the
	 * string. (TODO)
	 * Change this so that all syscalls with a pointer take a length.
	 *
	 * zra: I've added this user_mem_strlcpy, which I think eliminates the
     * the TOCTOU issue. Adding a length arg to this call would allow a more
	 * efficient implementation, though, since only one call to user_mem_check
	 * would be required.
	 */
	int ret = user_mem_strlcpy(p,tpath, path, MAX_PATH_LEN, PTE_USER_RO);
	int kfs_inode = kfs_lookup_path(tpath);
	if (kfs_inode < 0)
		return -EINVAL;
	struct proc *new_p = kfs_proc_create(kfs_inode);
	pid = new_p->pid;
	proc_decref(new_p, 1); // let go of the reference created in proc_create()
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
	spin_lock_irqsave(&p->proc_lock);
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
	spin_unlock_irqsave(&p->proc_lock);
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

	if (!p_to_die)
		return -EBADPROC;
	if (!proc_controls(p, p_to_die)) {
		proc_decref(p_to_die, 1);
		return -EPERM;
	}
	if (p_to_die == p) {
		// syscall code and pid2proc both have edible references, only need 1.
		p->exitcode = exitcode;
		proc_decref(p, 1);
		printd("[PID %d] proc exiting gracefully (code %d)\n", p->pid,exitcode);
	} else {
		panic("Destroying other processes is not supported yet.");
		//printk("[%d] destroying proc %d\n", p->pid, p_to_die->pid);
	}
	proc_destroy(p_to_die);
	return ESUCCESS;
}

static int sys_proc_yield(struct proc *p)
{
	proc_yield(p);
	return 0;
}

static ssize_t sys_run_binary(env_t* e, void *DANGEROUS binary_buf, size_t len,
                              void*DANGEROUS arg, size_t num_colors)
{
	env_t* env = proc_create(NULL,0);
	assert(env != NULL);

	static_assert(PROCINFO_NUM_PAGES == 1);
	assert(memcpy_from_user(e,env->env_procinfo->argv_buf,arg,PROCINFO_MAX_ARGV_SIZE) == ESUCCESS);
	*(intptr_t*)env->env_procinfo->env_buf = 0;

	env_load_icode(env,e,binary_buf,len);
	__proc_set_state(env, PROC_RUNNABLE_S);
	schedule_proc(env);
	if(num_colors > 0) {
		env->cache_colors_map = cache_colors_map_alloc();
		for(int i=0; i<num_colors; i++)
			cache_color_alloc(llc_cache, env->cache_colors_map);
	}
	proc_decref(env, 1);
	proc_yield(e);
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

	env_t* env = proc_create(NULL,0);
	assert(env != NULL);

	env->heap_bottom = e->heap_bottom;
	env->heap_top = e->heap_top;
	env->ppid = e->pid;
	env->env_tf = *current_tf;

	env->cache_colors_map = cache_colors_map_alloc();
	for(int i=0; i < llc_cache->num_colors; i++)
		if(GET_BITMASK_BIT(e->cache_colors_map,i))
			cache_color_alloc(llc_cache, env->cache_colors_map);

	int copy_page(env_t* e, pte_t* pte, void* va, void* arg)
	{
		env_t* env = (env_t*)arg;

		page_t* pp;
		if(upage_alloc(env,&pp,0))
			return -1;
		if(page_insert(env->env_pgdir,pp,va,*pte & PTE_PERM))
		{
			page_decref(pp);
			return -1;
		}

		pagecopy(page2kva(pp),ppn2kva(PTE2PPN(*pte)));
		return 0;
	}

	if(env_user_mem_walk(e,0,UTOP,&copy_page,env))
	{
		proc_decref(env,2);
		set_errno(current_tf,ENOMEM);
		return -1;
	}

	__proc_set_state(env, PROC_RUNNABLE_S);
	schedule_proc(env);

	// don't decref the new process.
	// that will happen when the parent waits for it.

	printd("[PID %d] fork PID %d\n",e->pid,env->pid);

	return env->pid;
}

static ssize_t sys_exec(env_t* e, void *DANGEROUS binary_buf, size_t len,
                        procinfo_t*DANGEROUS procinfo)
{
	// TODO: right now we only support exec for single-core processes
	if(e->state != PROC_RUNNING_S)
		return -1;

	if(memcpy_from_user(e,e->env_procinfo,procinfo,sizeof(*procinfo)))
		return -1;
	proc_init_procinfo(e);

	void* binary = kmalloc(len,0);
	if(binary == NULL)
		return -1;
	if(memcpy_from_user(e,binary,binary_buf,len))
	{
		kfree(binary);
		return -1;
	}

	env_segment_free(e,0,USTACKTOP);

	proc_init_trapframe(current_tf,0);
	env_load_icode(e,NULL,binary,len);

	kfree(binary);
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
	return mprotect(p, addr, len, prot);
}

static intreg_t sys_munmap(struct proc* p, void* addr, size_t len)
{
	return munmap(p, addr, len);
}

static void* sys_brk(struct proc *p, void* addr) {
	size_t range;

	if((addr < p->heap_bottom) || (addr >= (void*)USTACKBOT))
		goto out;

	if (addr > p->heap_top) {
		range = addr - p->heap_top;
		env_segment_alloc(p, p->heap_top, range);
	}
	else if (addr < p->heap_top) {
		range = p->heap_top - addr;
		env_segment_free(p, addr, range);
	}
	p->heap_top = addr;

out:
	return p->heap_top;
}

static ssize_t sys_shared_page_alloc(env_t* p1,
                                     void**DANGEROUS _addr, pid_t p2_id,
                                     int p1_flags, int p2_flags
                                    )
{
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


/************** Resource Request Syscalls **************/

/* sys_resource_req(): called directly from dispatch table. */

/************** Platform Specific Syscalls **************/

//Read a buffer over the serial port
static ssize_t sys_serial_read(env_t* e, char *DANGEROUS _buf, size_t len)
{
	if (len == 0)
		return 0;

	#ifdef SERIAL_IO
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
	#ifdef SERIAL_IO
		char *COUNT(len) _buf = user_mem_assert(e, buf, len, PTE_USER_RO);
		for(int i =0; i<len; i++)
			serial_send_byte(buf[i]);
		return (ssize_t)len;
	#else
		return -EINVAL;
	#endif
}

#ifdef __NETWORK__
// This is not a syscall we want. Its hacky. Here just for syscall stuff until get a stack.
static ssize_t sys_eth_read(env_t* e, char *DANGEROUS buf)
{
	extern int eth_up;

        extern uint32_t packet_buffer_count;
        extern char* packet_buffer[PACKET_BUFFER_SIZE];
        extern uint32_t packet_buffer_sizes[PACKET_BUFFER_SIZE];
        extern uint32_t packet_buffer_head;
        extern uint32_t packet_buffer_tail;
        extern spinlock_t packet_buffer_lock;

	if (eth_up) {

		uint32_t len;
		char *ptr;

		spin_lock(&packet_buffer_lock);

		if (packet_buffer_count == 0) {
			spin_unlock(&packet_buffer_lock);
			return 0;
		}

		ptr = packet_buffer[packet_buffer_head];
		len = packet_buffer_sizes[packet_buffer_head];

		packet_buffer_count = packet_buffer_count - 1;
		packet_buffer_head = (packet_buffer_head + 1) % PACKET_BUFFER_SIZE;

		spin_unlock(&packet_buffer_lock);

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
	extern int eth_up;

	if (eth_up) {

		if (len == 0)
			return 0;

		// HACK TO BYPASS HACK
		int just_sent = send_frame( buf, len);

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
			cur_packet_len = ((len - total_sent) > MAX_PACKET_DATA) ? MAX_PACKET_DATA : (len - total_sent);
			char* wrap_buffer = packet_wrap(_buf + total_sent, cur_packet_len);
			just_sent = send_frame(wrap_buffer, cur_packet_len + PACKET_HEADER_SIZE);

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

static ssize_t sys_eth_get_mac_addr(env_t* e, char *DANGEROUS buf) {
	
	extern int eth_up;

	if (eth_up) {
		extern char device_mac[];
		for (int i = 0; i < 6; i++)
			buf[i] = device_mac[i];
		return 0;
	}
	else
		return -EINVAL;
}


static int sys_eth_recv_check(env_t* e) {

	extern uint32_t packet_buffer_count;
	
	if (packet_buffer_count != 0) {
		return 1;
	}
	else
		return 0;
}

#endif // Network

/* sys_frontend_syscall_from_user(): called directly from dispatch table. */

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
		[SYS_run_binary] = (syscall_t)sys_run_binary,
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
	#ifdef __i386__
		[SYS_serial_read] = (syscall_t)sys_serial_read,
		[SYS_serial_write] = (syscall_t)sys_serial_write,
	#endif
	#ifdef __NETWORK__
		[SYS_eth_read] = (syscall_t)sys_eth_read,
		[SYS_eth_write] = (syscall_t)sys_eth_write,
		[SYS_eth_get_mac_addr] = (syscall_t)sys_eth_get_mac_addr,
		[SYS_eth_recv_check] = (syscall_t)sys_eth_recv_check,
	#endif
	#ifdef __sparc_v8__
		[SYS_frontend] = (syscall_t)frontend_syscall_from_user,
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
	#endif
	};

	const int max_syscall = sizeof(syscall_table)/sizeof(syscall_table[0]);

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
