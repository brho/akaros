#ifdef __SHARC__
#pragma nosharc
#endif

#ifdef __DEPUTY__
#pragma nodeputy
#endif

#include <atomic.h>
#include <process.h>
#include <kmalloc.h>
#include <pmap.h>
#include <frontend.h>
#include <syscall.h>
#include <smp.h>
#include <slab.h>
#include <arch/arch.h>

volatile int magic_mem[10];

void
frontend_proc_init(struct proc *SAFE p)
{
#ifdef __CONFIG_APPSERVER__
	pid_t parent_id = p->ppid, id = p->pid;
	int32_t errno;
	if(frontend_syscall(parent_id,APPSERVER_SYSCALL_proc_init,id,0,0,0,&errno))
		panic("Front-end server couldn't initialize new process!");
#endif
}

void
frontend_proc_free(struct proc *SAFE p)
{
#ifdef __CONFIG_APPSERVER__
	int32_t errno;
	if(frontend_syscall(0,APPSERVER_SYSCALL_proc_free,p->pid,0,0,0,&errno))
		panic("Front-end server couldn't free process!");
#endif
}

struct kmem_cache* struct_file_cache;
void file_init()
{
	struct_file_cache = kmem_cache_create("struct_file",
	                                      sizeof(struct file), 8, 0, 0, 0);
}

/* will zero anything in the page after the EOF */
error_t file_read_page(struct file* f, physaddr_t pa, size_t pgoff)
{
	int ret = frontend_syscall(0,APPSERVER_SYSCALL_pread,f->fd,pa,PGSIZE,
	                           pgoff*PGSIZE,NULL);
	if(ret >= 0)
		memset(KADDR(pa)+ret,0,PGSIZE-ret);
	return ret;
}

struct file* file_open_from_fd(struct proc* p, int fd)
{
	struct file* f = NULL;
	if(!(f = kmem_cache_alloc(struct_file_cache,0)))
		goto out;

	f->fd = frontend_syscall(p->pid,APPSERVER_SYSCALL_kdup,fd,0,0,0,NULL);
	if(f->fd == -1)
	{
		kmem_cache_free(struct_file_cache,f);
		f = NULL;
		goto out;
	}
	spinlock_init(&f->lock);
	f->refcnt = 1;

out:
	return f;
}

struct file* file_open(const char* path, int oflag, int mode)
{
	struct file* f = NULL;
	// although path is a kernel pointer, it may be below KERNBASE.
	// fix that if so.
	char* malloced = NULL;
	if((uintptr_t)path < KERNBASE)
	{
		size_t len = strlen(path)+1;
		malloced = kmalloc(len,0);
		if(!malloced)
			goto out;
		path = memcpy(malloced,path,len);
	}

	if(!(f = kmem_cache_alloc(struct_file_cache,0)))
		goto out;

	f->fd = frontend_syscall(0,APPSERVER_SYSCALL_open,PADDR(path),
	                         oflag,mode,0,NULL);
	if(f->fd == -1)
	{
		kmem_cache_free(struct_file_cache,f);
		f = NULL;
		goto out;
	}
	spinlock_init(&f->lock);
	f->refcnt = 1;

out:
	if(malloced)
		kfree(malloced);
	return f;
}

void file_incref(struct file* f)
{
	spin_lock(&f->lock);
	f->refcnt++;
	spin_unlock(&f->lock);
}

void file_decref(struct file* f)
{
	// if you decref too many times, you'll clobber memory :(
	spin_lock(&f->lock);
	if(--f->refcnt == 0)
	{
		int ret = frontend_syscall(0,APPSERVER_SYSCALL_close,f->fd,0,0,0,NULL);
		assert(ret == 0);
		kmem_cache_free(struct_file_cache,f);
	}
	else
		spin_unlock(&f->lock);
}

int frontend_syscall_errno(struct proc* p, int n, int a0, int a1, int a2, int a3)
{
	int errno, ret = frontend_syscall(p->pid,n,a0,a1,a2,a3,&errno);
	if(errno && p)
		set_errno(errno);
	return ret;
}

int32_t frontend_syscall(pid_t pid, int32_t syscall_num, 
                         uint32_t arg0, uint32_t arg1, 
                         uint32_t arg2, uint32_t arg3, int32_t* errno)
{
#ifndef __CONFIG_APPSERVER__
	warn("No appserver support, requested syscall %d for proc %d", syscall_num,
	     pid);
	if(errno)
		*errno = ENOSYS;
	return -1;
#endif

#ifdef __i386__
	if (!irq_is_enabled())
		warn("IRQ is disabled in frontend_syscall %d for proc %d\n", syscall_num, pid);
#endif

	static spinlock_t lock = SPINLOCK_INITIALIZER;
	int32_t ret;

	// only one frontend request at a time.
	// interrupts could try to do frontend requests,
	// which would deadlock, so disable them
	spin_lock(&lock);

	// write syscall into magic memory
	magic_mem[7] = 0;
	magic_mem[1] = syscall_num;
	magic_mem[2] = arg0;
	magic_mem[3] = arg1;
	magic_mem[4] = arg2;
	magic_mem[5] = arg3;
	magic_mem[6] = pid;
	magic_mem[0] = 0x80;

	// wait for front-end response
	while(magic_mem[7] == 0)
		;

	ret = magic_mem[1];
	if(errno)
		*errno = magic_mem[2];

	spin_unlock(&lock);

	return ret;
}

void __diediedie(trapframe_t* tf, uint32_t srcid, uint32_t code, uint32_t a1, uint32_t a2)
{
	int32_t errno;
	frontend_syscall(0,APPSERVER_SYSCALL_exit,(int)code,0,0,0,&errno);
	while(1);
}

void appserver_die(int code)
{
	int i;
	for(i = 0; i < num_cpus; i++)
		if(i != core_id())
			while(send_kernel_message(i,(amr_t)&__diediedie,(void*)code,0,0,
			                          KMSG_IMMEDIATE));

	// just in case.
	__diediedie(0,0,code,0,0);
}
