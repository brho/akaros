/* See COPYRIGHT for copyright information. */
/* Andrew Waterman <waterman@eecs.bekeley.edu> */

#include <arch/arch.h>
#include <arch/frontend.h>
#include <parlib.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include <sys/times.h>
#include <sys/time.h>
#include <debug.h>
#include <hart.h>
#include <utime.h>
#include <dirent.h>
#include <assert.h>
#include <stdlib.h>

// should kernel do V->P translation on these args?
#define IN0  1
#define IN1  2
#define IN2  4
#define OUT0 8
#define OUT1 16
#define OUT2 32

#define fe(n,x,y,z,trans) syscall(SYS_frontend,RAMP_SYSCALL_ ## n,(int)(x),(int)(y),(int)(z),trans)

#define getbuf(name,len) \
	assert(len <= PGSIZE); \
	char name##_blah[2*PGSIZE] __attribute__((aligned(8))); \
	char* name = (char*)(((uint32_t)name##_blah+PGSIZE)/PGSIZE*PGSIZE)
	
#define copy_if_off_page(ptr,len) \
	assert(len <= PGSIZE); \
	char buf##ptr[2*PGSIZE] __attribute__((aligned(8))); \
	if((uint32_t)ptr % sizeof(uint32_t) != 0 || ((uint32_t)ptr)/PGSIZE != ((uint32_t)ptr+len)/PGSIZE) \
	{ \
		char* buf2##ptr = (char*)(((uint32_t)buf##ptr+PGSIZE)/PGSIZE*PGSIZE); \
		memcpy(buf2##ptr,ptr,len); \
		ptr = buf2##ptr; \
	}

#define buf_if_off_page(ptr,len) \
	assert(len <= PGSIZE); \
	char buf##ptr [2*PGSIZE]; \
	char* buf2##ptr = (char*)(((uint32_t)buf##ptr+PGSIZE)/PGSIZE*PGSIZE); \
	void* old##ptr = ptr; \
	if((uint32_t)ptr % sizeof(uint32_t) != 0 || ((uint32_t)ptr)/PGSIZE != ((uint32_t)ptr+len)/PGSIZE) \
	{ \
		ptr = (typeof(ptr))(buf2##ptr); \
	}

#define copyout_if_off_page(ptr,len) \
	if((uint32_t)(old##ptr) % sizeof(uint32_t) != 0 || ((uint32_t)(old##ptr))/PGSIZE != ((uint32_t)ptr+len)/PGSIZE) \
	{ \
		memcpy(old##ptr,buf2##ptr,len); \
	}

/* Return the vcoreid, which is set in entry.S right before calling libmain.
 * This should only be used in libmain() and main(), before any code that might
 * use a register.  It just returns eax. */
uint32_t newcore(void)
{
	return hart_self();
}

mode_t
umask (mode_t mask)
{
	assert(0);
}

int
chmod (const char *name, mode_t mode)
{
	int len = strlen(name)+1;
	if(len > RAMP_MAXPATH)
		return -1;

	assert(0);
}

int
access (const char *name, int mode)
{
	int len = strlen(name)+1;
	if(len > RAMP_MAXPATH)
		return -1;

	assert(0);
}

char *
getwd (char *pwd)
{
	buf_if_off_page(pwd,RAMP_MAXPATH);
	int32_t ret = fe(getcwd,pwd,RAMP_MAXPATH,0,OUT0);
	copyout_if_off_page(pwd,RAMP_MAXPATH);
	return (char*)ret;
}

long int
pathconf (const char *pathname, int name)
{
	int len = strlen(pathname)+1;
	if(len > RAMP_MAXPATH)
		return -1;

	assert(0);
}

int
utime (const char *name, const struct utimbuf *buf)
{
	int len = strlen(name)+1;
	if(len > RAMP_MAXPATH)
		return -1;

	assert(0);
}

int
chown (const char *name, uid_t owner, gid_t group)
{
	int len = strlen(name)+1;
	if(len > RAMP_MAXPATH)
		return -1;

	assert(0);
}

int
mkdir (const char *name, mode_t mode)
{
	int len = strlen(name)+1;
	if(len > RAMP_MAXPATH)
		return -1;

	assert(0);
}


int
rmdir (const char *name)
{
	int len = strlen(name)+1;
	if(len > RAMP_MAXPATH)
		return -1;

	assert(0);
}

long int 
sysconf (int name)
{
	assert(0);
}

typedef struct
{
	int fd;
	struct dirent ent;
} __dir;

DIR *opendir (const char *name)
{
	__dir* dir = (__dir*)malloc(sizeof(__dir));
	if(dir == NULL)
		return NULL;

	int len = strlen(name)+1;
	if(len > RAMP_MAXPATH)
		return NULL;

	copy_if_off_page(name,len);
	dir->fd = fe(opendir,name,0,0,IN0);
	if(dir->fd < 0)
	{
		free(dir);
		return NULL;
	}

	return (DIR*)((char*)dir+1); // make dereferencing fail loudly
}

struct dirent *readdir (DIR *d)
{
	__dir* dir = (__dir*)((char*)d-1);
	struct dirent* dep = &dir->ent;

	buf_if_off_page(dep,sizeof(struct dirent));
	int ret = fe(readdir,dir->fd,dep,0,OUT1);
	copyout_if_off_page(dep,sizeof(struct dirent));

	return ret == 0 ? dep : 0;
}

void rewinddir (DIR *d)
{
	__dir* dir = (__dir*)((char*)d-1);
	fe(rewinddir,dir->fd,0,0,0);
}

int closedir (DIR *d)
{
	__dir* dir = (__dir*)((char*)d-1);
	int ret = fe(closedir,dir->fd,0,0,0);
	if(ret == 0)
		free(dir);
	return ret;
}

int pipe (int __fildes[2])
{
	assert(0);
}

int dup2 (int __fildes, int __fildes2)
{
	return fe(dup2,__fildes,__fildes2,0,0);
}

unsigned sleep (unsigned int __seconds)
{
	assert(0);
}

unsigned alarm(unsigned __secs)
{
	assert(0);
}

int execvp(const char *__file, char * const __argv[])
{
	assert(0);
}

int execv(const char *path, char *const argv[])
{
	assert(0);
}

int fcntl (int fd, int cmd, ...) 
{
	assert(0);
}

int chdir(const char *name)
{
	size_t len = strlen(name)+1;
	if(len > RAMP_MAXPATH)
		return -1;

	copy_if_off_page(name,len);
	return fe(chdir,name,0,0,IN0);
}

int
getpid(void)
{
	return procinfo.id;
}

void
_exit(int code)
{
	sys_proc_destroy(getpid());
	while(1);
}

int
isatty(int fd)
{
	struct stat s;
	int ret = fstat(fd,&s);
	return ret < 0 ? -1 : ((s.st_mode & S_IFCHR) ? 1 : 0);
}

int
fork(void)
{
	assert(0);
}

int
execve(const char* name, char* const argv[], char* const env[])
{
	assert(0);
}

int
kill(int pid, int sig)
{
	assert(0);
}

int
wait(int* status)
{
	assert(0);
}

int
link(const char *old, const char *new)
{
	int oldlen = strlen(old)+1, newlen = strlen(new)+1;
	if(oldlen > RAMP_MAXPATH || newlen > RAMP_MAXPATH)
		return -1;

	copy_if_off_page(old,oldlen);
	copy_if_off_page(new,oldlen);
	return fe(link,old,new,0,IN0 | IN1);
}

int
unlink(const char* name)
{
	int len = strlen(name)+1;
	if(len > RAMP_MAXPATH)
		return -1;

	copy_if_off_page(name,len);
	return fe(unlink,name,0,0,IN0);
}

int
fstat(int fd, struct stat* st)
{
	buf_if_off_page(st,sizeof(*st));
	int ret = fe(fstat,fd,st,0,OUT1);
	copyout_if_off_page(st,sizeof(*st));
	return ret;
}

int
lstat(const char* name, struct stat* st)
{
	int len = strlen(name)+1;
	if(len > RAMP_MAXPATH)
		return -1;

	copy_if_off_page(name,len);
	buf_if_off_page(st,sizeof(*st));
	int ret = fe(lstat,name,st,0,IN0 | OUT1);
	copyout_if_off_page(st,sizeof(*st));
	return ret;
}

int
stat(const char* name, struct stat* st)
{
	int len = strlen(name)+1;
	if(len > RAMP_MAXPATH)
		return -1;

	copy_if_off_page(name,len);
	buf_if_off_page(st,sizeof(*st));
	int ret = fe(stat,name,st,0,IN0 | OUT1);
	copyout_if_off_page(st,sizeof(*st));
	return ret;
}

off_t
lseek(int fd, off_t ptr, int dir)
{
	return fe(lseek,fd,ptr,dir,0);
}

ssize_t
write(int fd, const void* ptr, size_t len)
{
	len = MIN(PGSIZE,len);
	copy_if_off_page(ptr,len);
	return fe(write,fd,ptr,len,IN1);
}

ssize_t
read(int fd, void* ptr, size_t len)
{
	len = MIN(PGSIZE,len);
	buf_if_off_page(ptr,len);
	int ret = fe(read,fd,ptr,len,OUT1);
	copyout_if_off_page(ptr,len);
	return ret;
}

int
open(char* name, int flags, int mode)
{
	size_t len = strlen(name)+1;
	if(len > RAMP_MAXPATH)
		return -1;

	copy_if_off_page(name,len);
	return fe(open,name,flags,mode,IN0);
}

int
close(int fd)
{
	return fe(close,fd,0,0,0);
}

clock_t
times(struct tms* buf)
{
	extern struct timeval timeval_start;
	if(timeval_start.tv_sec == 0)
		return (clock_t)-1;

	struct timeval tp;
	if(gettimeofday(&tp,NULL))
		return (clock_t)-1;

	long long utime = (tp.tv_sec - timeval_start.tv_sec)*1000000;
	utime += tp.tv_usec-timeval_start.tv_usec;
	buf->tms_utime = buf->tms_cutime = utime*CLK_TCK/1000000;
	buf->tms_stime = buf->tms_cstime = 0;

	return (clock_t)buf->tms_utime;
}

int
gettimeofday(struct timeval* tp, void* tzp)
{
	static struct timeval tp0 __attribute__((aligned(sizeof(*tp))));
	if(tp0.tv_sec == 0)
	{
		int ret = fe(gettimeofday,&tp0,0,0,OUT0);
		if(ret)
			return ret;
	}

	long long dt = read_tsc();
	tp->tv_sec = tp0.tv_sec + dt/procinfo.tsc_freq;
	tp->tv_usec = (dt % procinfo.tsc_freq)*1000000/procinfo.tsc_freq;

	return 0;
}

