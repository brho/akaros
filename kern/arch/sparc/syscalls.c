#include <string.h>
#include <process.h>
#include <kmalloc.h>
#include <ros/error.h>
#include <pmap.h>
#include <arch/frontend.h>
#include <syscall.h>

void* user_memdup(struct proc* p, const void* va, int len)
{
	void* kva = NULL;
	if(len < 0 || (kva = kmalloc(len,0)) == NULL)
		return ERR_PTR(-ENOMEM);
	if(memcpy_from_user(p,kva,va,len))
	{
		kfree(kva);
		return ERR_PTR(-EINVAL);
	}

	return kva;
}

static void* user_memdup_errno(struct proc* p, const void* va, int len)
{
	void* kva = user_memdup(p,va,len);
	if(IS_ERR(kva))
	{
		set_errno(current_tf,-PTR_ERR(kva));
		return NULL;
	}
	return kva;
}

char* user_strdup(struct proc* p, const char* va0, int max)
{
	max++;
	char* kbuf = (char*)kmalloc(PGSIZE,0);
	if(kbuf == NULL)
		return ERR_PTR(-ENOMEM);

	int pos = 0, len = 0;
	const char* va = va0;
	while(max > 0 && len == 0)
	{
		int thislen = MIN(PGSIZE-(intptr_t)va%PGSIZE,max);
		if(memcpy_from_user(p,kbuf,va,thislen))
		{
			kfree(kbuf);
			return ERR_PTR(-EINVAL);
		}

		const char* nullterm = memchr(kbuf,0,thislen);
		if(nullterm)
			len = pos+(nullterm-kbuf)+1;

		pos += thislen;
		va += thislen;
		max -= thislen;
	}

	kfree(kbuf);
	return len ? user_memdup(p,va0,len) : ERR_PTR(-EINVAL);
}

static char* user_strdup_errno(struct proc* p, const char* va, int max)
{
	void* kva = user_strdup(p,va,max);
	if(IS_ERR(kva))
	{
		set_errno(current_tf,-PTR_ERR(kva));
		return NULL;
	}
	return kva;
}

static int memcpy_to_user_errno(struct proc* p, void* dst, const void* src,
                                int len)
{
	if(memcpy_to_user(p,dst,src,len))
	{
		set_errno(current_tf,EINVAL);
		return -1;
	}
	return 0;
}

static void* kmalloc_errno(int len)
{
	void* kva = NULL;
	if(len < 0 || (kva = kmalloc(len,0)) == NULL)
		set_errno(current_tf,ENOMEM);
	return kva;
}

int user_frontend_syscall_errno(struct proc* p, int n, int a0, int a1, int a2)
{
	int errno, ret = frontend_syscall(p->pid,n,a0,a1,a2,&errno);
	if(errno)
		set_errno(current_tf,errno);
	return ret;
}
#define fe(which,a0,a1,a2) \
	user_frontend_syscall_errno(p,RAMP_SYSCALL_##which,\
                                    (int)(a0),(int)(a1),(int)(a2))

intreg_t sys_write(struct proc* p, int fd, const void* buf, int len)
{
	void* kbuf = user_memdup_errno(p,buf,len);
	if(kbuf == NULL)
		return -1;
	int ret = fe(write,fd,PADDR(kbuf),len);
	kfree(kbuf);
	return ret;
}

intreg_t sys_read(struct proc* p, int fd, void* buf, int len)
{
	void* kbuf = kmalloc_errno(len);
	if(kbuf == NULL)
		return -1;
	int ret = fe(read,fd,PADDR(kbuf),len);
	if(ret != -1 && memcpy_to_user_errno(p,buf,kbuf,len))
		ret = -1;
	kfree(kbuf);
	return ret;
}

intreg_t sys_open(struct proc* p, const char* path, int oflag, int mode)
{
	char* fn = user_strdup_errno(p,path,PGSIZE);
	if(fn == NULL)
		return -1;
	int ret = fe(open,PADDR(fn),oflag,mode);
	kfree(fn);
	return ret;
}
intreg_t sys_close(struct proc* p, int fd)
{
	return fe(close,fd,0,0);
}

#define NEWLIB_STAT_SIZE 64
intreg_t sys_fstat(struct proc* p, int fd, void* buf)
{
	int kbuf[NEWLIB_STAT_SIZE/sizeof(int)];
	int ret = fe(fstat,fd,PADDR(kbuf),0);
	if(ret != -1 && memcpy_to_user_errno(p,buf,kbuf,NEWLIB_STAT_SIZE))
		ret = -1;
	return ret;
}

intreg_t sys_stat(struct proc* p, const char* path, void* buf)
{
	int kbuf[NEWLIB_STAT_SIZE/sizeof(int)];
	char* fn = user_strdup_errno(p,path,PGSIZE);
	if(fn == NULL)
		return -1;

	int ret = fe(stat,PADDR(fn),PADDR(kbuf),0);
	if(ret != -1 && memcpy_to_user_errno(p,buf,kbuf,NEWLIB_STAT_SIZE))
		ret = -1;

	kfree(fn);
	return ret;
}

intreg_t sys_lstat(struct proc* p, const char* path, void* buf)
{
	int kbuf[NEWLIB_STAT_SIZE/sizeof(int)];
	char* fn = user_strdup_errno(p,path,PGSIZE);
	if(fn == NULL)
		return -1;

	int ret = fe(lstat,PADDR(fn),PADDR(kbuf),0);
	if(ret != -1 && memcpy_to_user_errno(p,buf,kbuf,NEWLIB_STAT_SIZE))
		ret = -1;

	kfree(fn);
	return ret;
}

intreg_t sys_fcntl(struct proc* p, int fd, int cmd, int arg)
{
	return fe(fcntl,fd,cmd,arg);
}

intreg_t sys_access(struct proc* p, const char* path, int type)
{
	char* fn = user_strdup_errno(p,path,PGSIZE);
	if(fn == NULL)
		return -1;
	int ret = fe(access,PADDR(fn),type,0);
	kfree(fn);
	return ret;
}

intreg_t sys_umask(struct proc* p, int mask)
{
	return fe(umask,mask,0,0);
}

intreg_t sys_chmod(struct proc* p, const char* path, int mode)
{
	char* fn = user_strdup_errno(p,path,PGSIZE);
	if(fn == NULL)
		return -1;
	int ret = fe(chmod,PADDR(fn),mode,0);
	kfree(fn);
	return ret;
}

intreg_t sys_lseek(struct proc* p, int fd, int offset, int whence)
{
	return fe(lseek,fd,offset,whence);
}

intreg_t sys_link(struct proc* p, const char* _old, const char* _new)
{
	char* oldpath = user_strdup_errno(p,_old,PGSIZE);
	if(oldpath == NULL)
		return -1;

	char* newpath = user_strdup_errno(p,_new,PGSIZE);
	if(newpath == NULL)
	{
		kfree(oldpath);
		return -1;
	}

	int ret = fe(link,PADDR(oldpath),PADDR(newpath),0);
	kfree(oldpath);
	kfree(newpath);
	return ret;
}

intreg_t sys_unlink(struct proc* p, const char* path)
{
	char* fn = user_strdup_errno(p,path,PGSIZE);
	if(fn == NULL)
		return -1;
	int ret = fe(unlink,PADDR(fn),0,0);
	kfree(fn);
	return ret;
}

intreg_t sys_chdir(struct proc* p, const char* path)
{
	char* fn = user_strdup_errno(p,path,PGSIZE);
	if(fn == NULL)
		return -1;
	int ret = fe(chdir,PADDR(fn),0,0);
	kfree(fn);
	return ret;
}

intreg_t sys_getcwd(struct proc* p, char* pwd, int size)
{
	void* kbuf = kmalloc_errno(size);
	if(kbuf == NULL)
		return -1;
	int ret = fe(read,PADDR(kbuf),size,0);
	if(ret != -1 && memcpy_to_user_errno(p,pwd,kbuf,strnlen(kbuf,size)))
		ret = -1;
	kfree(kbuf);
	return ret;
}

intreg_t sys_gettimeofday(struct proc* p, int* buf)
{
	static spinlock_t gtod_lock = SPINLOCK_INITIALIZER;
	static int t0 = 0;

	spin_lock(&gtod_lock);
	if(t0 == 0)
		t0 = fe(time,0,0,0);
	spin_unlock(&gtod_lock);

	long long dt = read_tsc();
	int kbuf[2] = {t0+dt/system_timing.tsc_freq,
	    (dt%system_timing.tsc_freq)*1000000/system_timing.tsc_freq};

	return memcpy_to_user_errno(p,buf,kbuf,sizeof(kbuf));
}

