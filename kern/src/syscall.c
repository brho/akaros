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
#include <kfs.h> // eventually replace this with vfs.h

#ifdef __NETWORK__
#include <arch/nic_common.h>
extern char *CT(PACKET_HEADER_SIZE + len) (*packet_wrap)(const char *CT(len) data, size_t len);
extern int (*send_frame)(const char *CT(len) data, size_t len);
#endif

static void sys_yield(struct proc *p);

//Do absolutely nothing.  Used for profiling.
static void sys_null(void)
{
	return;
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

//
/* START OF REMOTE SYSTEMCALL SUPPORT SYSCALLS. THESE WILL GO AWAY AS THINGS MATURE */
//

static ssize_t sys_run_binary(env_t* e, void *DANGEROUS binary_buf,
                              void*DANGEROUS arg, size_t len) {
	uint8_t *CT(len) checked_binary_buf;
	checked_binary_buf = user_mem_assert(e, binary_buf, len, PTE_USER_RO);

	uint8_t* new_binary = kmalloc(len, 0);
	if(new_binary == NULL)
		return -ENOMEM;
	memcpy(new_binary, checked_binary_buf, len);

	env_t* env = env_create(new_binary, len);
	kfree(new_binary);
	proc_set_state(env, PROC_RUNNABLE_S);
	schedule_proc(env);
	sys_yield(e);
	
	return 0;
}

#ifdef __NETWORK__
// This is not a syscall we want. Its hacky. Here just for syscall stuff until get a stack.
static ssize_t sys_eth_write(env_t* e, const char *DANGEROUS buf, size_t len) 
{ 
	extern int eth_up;
	
	if (eth_up) {
		
		if (len == 0)
			return 0;
		
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
		
	}
	else
		return -EINVAL;
}

// This is not a syscall we want. Its hacky. Here just for syscall stuff until get a stack.
static ssize_t sys_eth_read(env_t* e, char *DANGEROUS buf, size_t len) 
{
	extern int eth_up;

	if (eth_up) {
		extern int packet_waiting;
		extern int packet_buffer_size;
		extern char*CT(packet_buffer_size) packet_buffer;
		extern char*CT(MAX_FRAME_SIZE) packet_buffer_orig;
		extern int packet_buffer_pos;

		if (len == 0)
			return 0;

		char *CT(len) _buf = user_mem_assert(e, buf,len, PTE_U);
			
		if (packet_waiting == 0)
			return 0;
			
		int read_len = ((packet_buffer_pos + len) > packet_buffer_size) ? packet_buffer_size - packet_buffer_pos : len;

		memcpy(_buf, packet_buffer + packet_buffer_pos, read_len);
	
		packet_buffer_pos = packet_buffer_pos + read_len;
	
		if (packet_buffer_pos == packet_buffer_size) {
			kfree(packet_buffer_orig);
			packet_waiting = 0;
		}
	
		return read_len;
	}
	else
		return -EINVAL;
}
#endif // Network

//
/* END OF REMOTE SYSTEMCALL SUPPORT SYSCALLS. */
//

static ssize_t sys_shared_page_alloc(env_t* p1,
                                     void**DANGEROUS _addr, envid_t p2_id,
                                     int p1_flags, int p2_flags
                                    )
{
	//if (!VALID_USER_PERMS(p1_flags)) return -EPERM;
	//if (!VALID_USER_PERMS(p2_flags)) return -EPERM;

	void * COUNT(1) * COUNT(1) addr = user_mem_assert(p1, _addr, sizeof(void *), 
                                                      PTE_USER_RW);
	page_t* page;
	env_t* p2 = &(envs[ENVX(p2_id)]);
	error_t e = page_alloc(&page);

	if(e < 0) return e;

	void* p2_addr = page_insert_in_range(p2->env_pgdir, page,
	                                     (void*SNT)UTEXT, (void*SNT)UTOP, p2_flags);
	if(p2_addr == NULL)
		return -EFAIL;

	void* p1_addr = page_insert_in_range(p1->env_pgdir, page,
	                                    (void*SNT)UTEXT, (void*SNT)UTOP, p1_flags);
	if(p1_addr == NULL) {
		page_remove(p2->env_pgdir, p2_addr);
		return -EFAIL;
	}
	*addr = p1_addr;
	return ESUCCESS;
}

static void sys_shared_page_free(env_t* p1, void*DANGEROUS addr, envid_t p2)
{
}

// Invalidate the cache of this core.  Only useful if you want a cold cache for
// performance testing reasons.
static void sys_cache_invalidate(void)
{
	#ifdef __i386__
		wbinvd();
	#endif
	return;
}

// Writes 'val' to 'num_writes' entries of the well-known array in the kernel
// address space.  It's just #defined to be some random 4MB chunk (which ought
// to be boot_alloced or something).  Meant to grab exclusive access to cache
// lines, to simulate doing something useful.
static void sys_cache_buster(env_t* e, uint32_t num_writes, uint32_t num_pages,
                             uint32_t flags)
{ TRUSTEDBLOCK /* zra: this is not really part of the kernel */
	#define BUSTER_ADDR		0xd0000000  // around 512 MB deep
	#define MAX_WRITES		1048576*8
	#define MAX_PAGES		32
	#define INSERT_ADDR 	(UINFO + 2*PGSIZE) // should be free for these tests
	uint32_t* buster = (uint32_t*)BUSTER_ADDR;
	static uint32_t buster_lock = 0;
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
			page_alloc(&a_page[i]);
			page_insert(e->env_pgdir, a_page[i], (void*)INSERT_ADDR + PGSIZE*i,
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
			page_remove(e->env_pgdir, (void*)(INSERT_ADDR + PGSIZE * i));
			page_decref(a_page[i]);
		}
		spin_unlock(&buster_lock);
	}

	/* Print info */
	if (flags & BUSTER_PRINT_TICKS) {
		ticks = stop_timing(ticks);
		printk("%llu,", ticks);
	}
	return;
}

// Print a string to the system console.
// The string is exactly 'len' characters long.
// Destroys the environment on memory errors.
static ssize_t sys_cputs(env_t* e, const char *DANGEROUS s, size_t len)
{
	// Check that the user has permission to read memory [s, s+len).
	// Destroy the environment if not.
	pte_t* p = pgdir_walk(e->env_pgdir,s,0);
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

// Returns the current environment's envid.
static envid_t sys_getenvid(env_t* e)
{
	return e->env_id;
}

// Returns the id of the cpu this syscall is executed on.
static envid_t sys_getcpuid(void)
{
	return core_id();
}

// TODO FIX Me!!!! for processes
// Destroy a given environment (possibly the currently running environment).
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-EBADENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
static error_t sys_env_destroy(env_t* e, envid_t envid)
{
	int r;
	env_t *env_to_die;

	if ((r = envid2env(envid, &env_to_die, 1)) < 0)
		return r;
	if (env_to_die == e)
		printk("[%08x] exiting gracefully\n", e->env_id);
	else
		panic("Destroying other processes is not supported yet.");
		//printk("[%08x] destroying %08x\n", e->env_id, env_to_die->env_id);
	proc_destroy(env_to_die);
	return ESUCCESS;
}

/*
 * Current process yields its remaining "time slice".  Currently works for
 * single-core processes.
 * TODO: think about how this works with async calls and multicored procs.
 * Want it to only be callable locally.
 */
static void sys_yield(struct proc *p)
{
	// This is all standard single-core, local call
	spin_lock_irqsave(&p->proc_lock);
	assert(p->state == PROC_RUNNING_S);
	proc_set_state(p, PROC_RUNNABLE_S);
	schedule_proc(p);
	spin_unlock_irqsave(&p->proc_lock);
	// the implied thing here is that all state has been saved before leaving
	// could do the "leaving the process context" here, mentioned in startcore
	schedule();

	/* TODO
	 * if running_s, give up your time slice (schedule, save silly state, block)
	 * if running_m and 2+ cores are left, give yours up, stay running_m
	 * if running_m and last core, switch to runnable_s
	 */
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
	return new_p->env_id; // TODO replace this with a real proc_id
}

/* Makes process PID runnable.  Consider moving the functionality to env.c */
static error_t sys_proc_run(struct proc *p, unsigned pid)
{
	struct proc *target = get_proc(pid);
	error_t retval = 0;
	spin_lock_irqsave(&p->proc_lock); // note we can get interrupted here. it's not bad.
	// make sure we have access and it's in the right state to be activated
	if (!proc_controls(p, target)) {
		retval = -EPERM;
	} else if (target->state != PROC_CREATED) {
		retval = -EINVAL;
	} else {
		proc_set_state(target, PROC_RUNNABLE_S);
		schedule_proc(target);
	}
	spin_unlock_irqsave(&p->proc_lock);
	return retval;
}

// TODO: Build a dispatch table instead of switching on the syscallno
// Dispatches to the correct kernel function, passing the arguments.
intreg_t syscall(env_t* e, uintreg_t syscallno, uintreg_t a1, uintreg_t a2,
                 uintreg_t a3, uintreg_t a4, uintreg_t a5)
{
	// Call the function corresponding to the 'syscallno' parameter.
	// Return any appropriate return value.

	//cprintf("Incoming syscall number: %d\n    a1: %x\n   "
	//        " a2: %x\n    a3: %x\n    a4: %x\n    a5: %x\n",
	//        syscallno, a1, a2, a3, a4, a5);

	// used if we need more args, like in mmap
	int32_t _a4, _a5, _a6, *COUNT(3) args;

	assert(e); // should always have an env for every syscall
	//printk("Running syscall: %d\n", syscallno);
	if (INVALID_SYSCALL(syscallno))
		return -EINVAL;

	switch (syscallno) {
		case SYS_null:
			sys_null();
			return ESUCCESS;
		case SYS_cache_buster:
			sys_cache_buster(e, a1, a2, a3);
			return 0;
		case SYS_cache_invalidate:
			sys_cache_invalidate();
			return 0;
		case SYS_shared_page_alloc:
			return sys_shared_page_alloc(e, (void** DANGEROUS) a1,
		                                 a2, (int) a3, (int) a4);
		case SYS_shared_page_free:
			sys_shared_page_free(e, (void* DANGEROUS) a1, a2);
		    return ESUCCESS;
		case SYS_cputs:
			return sys_cputs(e, (char *DANGEROUS)a1, (size_t)a2);
		case SYS_cgetc:
			return sys_cgetc(e);
		case SYS_getcpuid:
			return sys_getcpuid();
		case SYS_getpid:
			return sys_getenvid(e);
		case SYS_proc_destroy:
			return sys_env_destroy(e, (envid_t)a1);
		case SYS_yield:
			sys_yield(e);
			return ESUCCESS;
		case SYS_proc_create:
			return sys_proc_create(e, (char *DANGEROUS)a1);
		case SYS_proc_run:
			return sys_proc_run(e, (size_t)a1);
		case SYS_mmap:
			// we only have 4 parameters from sysenter currently, need to copy
			// in the others.  if we stick with this, we can make a func for it.
    		args = user_mem_assert(e, (void*DANGEROUS)a4,
			                       3*sizeof(_a4), PTE_USER_RW);
			_a4 = args[0];
			_a5 = args[1];
			_a6 = args[2];
			return (intreg_t) mmap(e, a1, a2, a3, _a4, _a5, _a6);
		case SYS_brk:
			printk("brk not implemented yet\n");
			return -EINVAL;

	#ifdef __i386__
		case SYS_serial_write:
			return sys_serial_write(e, (char *DANGEROUS)a1, (size_t)a2);
		case SYS_serial_read:
			return sys_serial_read(e, (char *DANGEROUS)a1, (size_t)a2);
		case SYS_run_binary:
			return sys_run_binary(e, (char *DANGEROUS)a1,
			                      (char* DANGEROUS)a2, (size_t)a3);
	#endif
	#ifdef __NETWORK__
		case SYS_eth_write:
			return sys_eth_write(e, (char *DANGEROUS)a1, (size_t)a2);
		case SYS_eth_read:
			return sys_eth_read(e, (char *DANGEROUS)a1, (size_t)a2);
	#endif
	#ifdef __sparc_v8__
		case SYS_frontend:
			return frontend_syscall(a1,a2,a3,a4);
	#endif

		default:
			// or just return -EINVAL
			panic("Invalid syscall number %d for env %x!", syscallno, *e);
	}
	return 0xdeadbeef;
}

intreg_t syscall_async(env_t* e, syscall_req_t *call)
{
	return syscall(e, call->num, call->args[0], call->args[1],
	               call->args[2], call->args[3], call->args[4]);
}

intreg_t process_generic_syscalls(env_t* e, size_t max)
{
	size_t count = 0;
	syscall_back_ring_t* sysbr = &e->syscallbackring;

	// make sure the env is still alive.
	// incref will return ESUCCESS on success.
	if (proc_incref(e))
		return -EFAIL;

	// max is the most we'll process.  max = 0 means do as many as possible
	while (RING_HAS_UNCONSUMED_REQUESTS(sysbr) && ((!max)||(count < max)) ) {
		if (!count) {
			// ASSUME: one queue per process
			// only switch cr3 for the very first request for this queue
			// need to switch to the right context, so we can handle the user pointer
			// that points to a data payload of the syscall
			lcr3(e->env_cr3);
		}
		count++;
		//printk("DEBUG PRE: sring->req_prod: %d, sring->rsp_prod: %d\n",
		//	   sysbr->sring->req_prod, sysbr->sring->rsp_prod);
		// might want to think about 0-ing this out, if we aren't
		// going to explicitly fill in all fields
		syscall_rsp_t rsp;
		// this assumes we get our answer immediately for the syscall.
		syscall_req_t* req = RING_GET_REQUEST(sysbr, ++(sysbr->req_cons));
		rsp.retval = syscall_async(e, req);
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
	proc_decref(e);
	return (intreg_t)count;
}
