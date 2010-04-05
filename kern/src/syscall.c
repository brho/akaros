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
extern char device_mac[6];
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
                              procinfo_t*DANGEROUS procinfo, size_t num_colors)
{
	env_t* env = proc_create(NULL,0);
	assert(env != NULL);

	if(memcpy_from_user(e,e->env_procinfo,procinfo,sizeof(*procinfo)))
		return -1;
	proc_init_procinfo(e);

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
		}
		else // PAGE_PAGED_OUT(*pte)
		{
			pte_t* newpte = pgdir_walk(env->env_pgdir,va,1);
			if(!newpte)
				return -1;

			struct file* file = PTE2PFAULT_INFO(*pte)->file;
			pfault_info_t* newpfi = pfault_info_alloc(file);
			if(!newpfi)
				return -1;

			*newpfi = *PTE2PFAULT_INFO(*pte);
			*newpte = PFAULT_INFO2PTE(newpfi);
		}

		return 0;
	}

	// copy procdata and procinfo
	memcpy(env->env_procdata,e->env_procdata,sizeof(struct procdata));
	memcpy(env->env_procinfo,e->env_procinfo,sizeof(struct procinfo));
	env->env_procinfo->pid = env->pid;
	env->env_procinfo->ppid = env->ppid;

	// copy all memory below procdata
	if(env_user_mem_walk(e,0,UDATA,&copy_page,env))
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

intreg_t sys_exec(struct proc* p, int fd, procinfo_t* pi)
{
	if(p->state != PROC_RUNNING_S)
		return -1;

	int ret = -1;
	struct file* f = file_open_from_fd(p,fd);
	if(f == NULL) {
		set_errno(current_tf, EBADF);
		goto out;
	}

	if(memcpy_from_user(p,p->env_procinfo,pi,sizeof(procinfo_t))) {
		proc_destroy(p);
		goto out;
	}
	proc_init_procinfo(p);
	memset(p->env_procdata, 0, sizeof(procdata_t));

	env_user_mem_free(p,0,USTACKTOP);

	if(load_elf(p,f))
	{
		proc_destroy(p);
		goto out;
	}
	file_decref(f);
	*current_tf = p->env_tf;
	ret = 0;

	printd("[PID %d] exec fd %d\n",p->pid,fd);

out:
	return ret;
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
	ssize_t range;

	spin_lock_irqsave(&p->proc_lock);

	if((addr < p->env_procinfo->heap_bottom) || (addr >= (void*)BRK_END))
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
		if(__munmap(p, (void*)real_new_heap_top, -range))
			goto out;
	}
	p->heap_top = addr;

out:
	spin_unlock_irqsave(&p->proc_lock);
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
	void* kbuf = user_memdup_errno(p,buf,len);
	if(kbuf == NULL)
		return -1;
	int ret = ufe(write,fd,PADDR(kbuf),len,0);
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
	char* fn = user_strdup_errno(p,path,PGSIZE);
	if(fn == NULL)
		return -1;
	int ret = ufe(open,PADDR(fn),oflag,mode,0);
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
#ifdef __CONFIG_APPSERVER__
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

