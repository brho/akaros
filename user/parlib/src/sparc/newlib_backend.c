/* See COPYRIGHT for copyright information. */
/* Andrew Waterman <waterman@eecs.bekeley.edu> */

#include <sys/fcntl.h>
#include <stdio.h>
#include <arch/arch.h>
#include <arch/frontend.h>
#include <parlib.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include <sys/times.h>
#include <sys/wait.h>
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
	
#define memcpy_if_off_page(ptr,len) \
	assert(len <= PGSIZE); \
	char buf##ptr[2*PGSIZE] __attribute__((aligned(8))); \
	if((uint32_t)ptr % sizeof(uint32_t) != 0 || ((uint32_t)ptr)/PGSIZE != ((uint32_t)ptr+len)/PGSIZE) \
	{ \
		char* buf2##ptr = (char*)(((uint32_t)buf##ptr+PGSIZE)/PGSIZE*PGSIZE); \
		memcpy(buf2##ptr,ptr,len); \
		ptr = buf2##ptr; \
	}

#define strcpy_if_off_page(ptr,len) \
	assert(len <= PGSIZE); \
	char buf##ptr[2*PGSIZE] __attribute__((aligned(8))); \
	if((uint32_t)ptr % sizeof(uint32_t) != 0 || ((uint32_t)ptr)/PGSIZE != ((uint32_t)ptr+len)/PGSIZE) \
	{ \
		char* buf2##ptr = (char*)(((uint32_t)buf##ptr+PGSIZE)/PGSIZE*PGSIZE); \
		strcpy(buf2##ptr,ptr); \
		ptr = buf2##ptr; \
	}

#define buf_if_off_page(ptr,len) \
	assert(len <= PGSIZE); \
	char buf##ptr [2*PGSIZE] __attribute__((aligned(8))); \
	char* buf2##ptr = (char*)(((uint32_t)buf##ptr+PGSIZE)/PGSIZE*PGSIZE); \
	void* old##ptr = ptr; \
	if((uint32_t)ptr % sizeof(uint32_t) != 0 || ((uint32_t)ptr)/PGSIZE != ((uint32_t)ptr+len)/PGSIZE) \
	{ \
		ptr = (typeof(ptr))(buf2##ptr); \
	}

#define copyout_if_off_page(ptr,len) \
	if((uint32_t)(old##ptr) % sizeof(uint32_t) != 0 || ((uint32_t)(old##ptr))/PGSIZE != ((uint32_t)(old##ptr)+len)/PGSIZE) \
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
	return fe(umask,mask,0,0,0);
}

int
chmod (const char *name, mode_t mode)
{
	int len = strlen(name)+1;
	if(len > RAMP_MAXPATH)
		return -1;

	strcpy_if_off_page(name,RAMP_MAXPATH);
	return fe(chmod,name,mode,0,IN0);
}

int
access (const char *name, int mode)
{
	int len = strlen(name)+1;
	if(len > RAMP_MAXPATH)
		return -1;

	strcpy_if_off_page(name,RAMP_MAXPATH);
	return fe(access,name,mode,0,IN0);
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
	assert(sizeof(time_t) == sizeof(int));
	time_t actime = buf == NULL ? time(NULL) : buf->actime;
	time_t modtime = buf == NULL ? actime : buf->modtime;

	int len = strlen(name)+1;
	if(len > RAMP_MAXPATH)
		return -1;
	strcpy_if_off_page(name,RAMP_MAXPATH);

	return fe(utime,name,actime,modtime,IN0);
}

uid_t
getuid()
{
	return 0;
}

uid_t
geteuid()
{
	return 0;
}

gid_t
getgid()
{
	return 0;
}

gid_t
getegid()
{
	return 0;
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
	switch(name)
	{
		case _SC_CLK_TCK:
			return procinfo.tsc_freq;
		case _SC_PAGESIZE:
			return PGSIZE;
		case _SC_PHYS_PAGES:
			return 512*1024; // 2GB mem
		default:
			printf("sysconf(%d) not supported!\n",name);
			abort();
	}
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

	strcpy_if_off_page(name,RAMP_MAXPATH);
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

int dup (int __fildes)
{
	return fe(dup,__fildes,0,0,0);
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

int execvp(const char *file, char * const argv[])
{
	if(file[0] == '/')
		return execv(file,argv);

	// this is technically incorrect, because we need to search PATH
	const char* path = getenv("PATH");
	if(path == NULL)
		path = ":/bin:/usr/bin";
	char* buf = (char*)malloc((strlen(path)+strlen(file)+2)*sizeof(char));

	char* dir = path;
	while(1)
	{
		char* end = strchr(dir,':');
		int len = end ? end-dir : strlen(dir);
		memcpy(buf,dir,len);
		if(len && buf[len-1] != '/')
			buf[len++] = '/';
		strcpy(buf+len,file);
	
		if(access(buf,X_OK) == 0)
		{
			int ret = execv(buf,argv);
			free(buf);
			return ret;
		}

		if(!end)
			break;

		dir = end+1;
	}

	free(buf);
	errno = ENOENT;
	return -1;
}

int execv(const char *path, char *const argv[])
{
	return execve(path,argv,environ);
}

int fcntl (int fd, int cmd, ...)
{
	va_list vl;
	va_start(vl,cmd);
	int arg = va_arg(vl,int);
	va_end(vl);

	switch(cmd)
	{
		case F_DUPFD:
		case F_GETFD:
		case F_SETFD:
			return fe(fcntl,fd,cmd,arg,0);
		default:
			printf("fcntl(%d,%d) not supported!\n",fd,cmd);
			abort();
	}
}

int chdir(const char *name)
{
	size_t len = strlen(name)+1;
	if(len > RAMP_MAXPATH)
		return -1;

	strcpy_if_off_page(name,RAMP_MAXPATH);
	return fe(chdir,name,0,0,IN0);
}

int
getppid(void)
{
	return procinfo.ppid;
}

int
getpid(void)
{
	return procinfo.pid;
}

void
_exit(int code)
{
	sys_proc_destroy(getpid(),code);
	while(1);
}

int
isatty(int fd)
{
	struct stat s;
	int ret = fstat(fd,&s);
	return ret < 0 ? -1 : ((s.st_mode & S_IFCHR) ? 1 : 0);
}

static hart_lock_t child_lock = HART_LOCK_INIT;
static int* child_list = NULL;
static int child_list_capacity = 0;
static int child_list_size = 0;

int
fork(void)
{
	hart_lock_lock(&child_lock);
	if(child_list_size == child_list_capacity)
	{
		child_list_capacity++;
		int* tmp = realloc(child_list,child_list_capacity*sizeof(int));
		if(tmp == NULL)
		{
			child_list_capacity--;
			errno = ENOMEM;
			hart_lock_unlock(&child_lock);
			return -1;
		}
		child_list = tmp;
	}

	int ret = syscall(SYS_fork,0,0,0,0,0);

	if(ret > 0)
		child_list[child_list_size++] = ret;

	hart_lock_unlock(&child_lock);
	return ret;
}

static int
pack_argv(const char* const argv[], void* base, char* buf, size_t bufsz)
{
	int argc = 0, size = sizeof(intreg_t);
	while(argv[argc])
	{
		size += sizeof(intreg_t)+strlen(argv[argc])+1;
		argc++;
	}

	if(size > bufsz)
		return -1;

	intreg_t* offset = (intreg_t*)buf;
	offset[0] = (argc+1)*sizeof(intreg_t)+(intreg_t)base;
	for(int i = 0; i < argc; i++)
	{
		int len = strlen(argv[i])+1;
		memcpy(buf+offset[i]-(intreg_t)base,argv[i],len);
		offset[i+1] = offset[i]+len;
	}
	offset[argc] = 0;

	return 0;
}

static int
readfile(const char* filename, void** binary, int* size)
{
	int fd = open(filename,O_RDONLY,0);
	if(fd == -1)
		return -1;

	*size = 0;
	*binary = NULL;
	int bytes_read = 0;
	int bufsz = 0;

	int READ_SIZE = 1024;
	int MALLOC_SIZE = 1024*1024;

	while(1)
	{
		if(*size+READ_SIZE > bufsz)
		{
			void* temp_buf = realloc(*binary,bufsz+MALLOC_SIZE);
			if(temp_buf == NULL)
			{
				close(fd);
				free(*binary);
				errno = ENOMEM;
				return -1;
			}

			*binary = temp_buf;
			bufsz += MALLOC_SIZE;
		}

		bytes_read = read(fd, *binary+*size, READ_SIZE);
		*size += bytes_read;
		if(bytes_read <= 0)
		{
			close(fd);
			if(bytes_read < 0)
				free(*binary);
			return bytes_read;
		}
	}
}

int
execve(const char* name, char* const argv[], char* const env[])
{
	procinfo_t pi;
	if(pack_argv(argv,procinfo.argv_buf,pi.argv_buf,PROCINFO_MAX_ARGV_SIZE)
	   || pack_argv(env,procinfo.env_buf,pi.env_buf,PROCINFO_MAX_ENV_SIZE))
	{
		errno = ENOMEM;
		return -1;
	}

	void* binary;
	size_t binarysz;
	if(readfile(name,&binary,&binarysz))
		return -1;

	return syscall(SYS_exec,(intreg_t)binary,(intreg_t)binarysz,
                       (intreg_t)&pi,0,0);
}

int
kill(int pid, int sig)
{
	int ret = sys_proc_destroy(pid,0);
	return ret < 0 ? -1 : ret;
}

int
waitpid(int pid, int* status, int options)
{
	assert(options == 0);

	int foo;
	if(status == NULL)
		status = &foo;

	hart_lock_lock(&child_lock);

	if(child_list_size) while(1)
	{
		for(int i = 0; i < child_list_size; i++)
		{
			if(pid == -1 || child_list[i] == pid)
			{
				int ret = syscall(SYS_trywait,child_list[i],status,0,0,0);

				if(ret == 0)
				{
					for(int j = i+1; j < child_list_size; j++)
						child_list[j-1] = child_list[j];
					child_list_size--;
					hart_lock_unlock(&child_lock);
					return 0;
				}
			}
		}
		sys_yield();
	}

	hart_lock_unlock(&child_lock);
	errno = ECHILD;
	return -1;
}

int
wait(int* status)
{
	return waitpid(-1,status,0);
}

int
link(const char *old, const char *new)
{
	int oldlen = strlen(old)+1, newlen = strlen(new)+1;
	if(oldlen > RAMP_MAXPATH || newlen > RAMP_MAXPATH)
		return -1;

	strcpy_if_off_page(old,RAMP_MAXPATH);
	strcpy_if_off_page(new,RAMP_MAXPATH);
	return fe(link,old,new,0,IN0 | IN1);
}

int
unlink(const char* name)
{
	int len = strlen(name)+1;
	if(len > RAMP_MAXPATH)
		return -1;

	strcpy_if_off_page(name,RAMP_MAXPATH);
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

	strcpy_if_off_page(name,RAMP_MAXPATH);
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

	strcpy_if_off_page(name,RAMP_MAXPATH);
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
	return syscall(SYS_write,fd,ptr,len,0,0);

	for(int pos = 0; pos < len; )
	{
		int thislen = MIN(PGSIZE,len-pos);
		const void* thisptr = ptr+pos;

		memcpy_if_off_page(thisptr,thislen);
		int ret = fe(write,fd,thisptr,thislen,IN1);

		if(ret == -1)
			return -1;

		pos += ret;
		if(ret < thislen)
			return pos;
	}
	return len;
}

ssize_t
read(int fd, void* ptr, size_t len)
{
	return syscall(SYS_read,fd,ptr,len,0,0);

	for(int pos = 0; pos < len; )
	{
		int thislen = MIN(PGSIZE,len-pos);
		const void* thisptr = ptr+pos;

		buf_if_off_page(thisptr,thislen);
		int ret = fe(read,fd,thisptr,thislen,OUT1);
		copyout_if_off_page(thisptr,thislen);

		if(ret == -1)
			return -1;

		pos += ret;
		if(ret < thislen)
			return pos;
	}

	return len;
}

int
open(const char* name, int flags, ...)
{
	int mode;
	if(flags & O_CREAT)
	{
		va_list vl;
		va_start(vl,flags);
		mode = va_arg(vl,int);
		va_end(vl);
	}
	return syscall(SYS_open,name,flags,mode,0,0);

	size_t len = strlen(name)+1;
	if(len > RAMP_MAXPATH)
		return -1;

	strcpy_if_off_page(name,RAMP_MAXPATH);
	return fe(open,name,flags,mode,IN0);
}

int
close(int fd)
{
	return syscall(SYS_close,fd,0,0,0,0);

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

	unsigned long long utime = (tp.tv_sec - timeval_start.tv_sec)*1000000;
	utime += tp.tv_usec-timeval_start.tv_usec;
	buf->tms_utime = buf->tms_cutime = utime*procinfo.tsc_freq/1000000;
	buf->tms_stime = buf->tms_cstime = 0;

	return (clock_t)buf->tms_utime;
}

int
gettimeofday(struct timeval* tp, void* tzp)
{
	static struct timeval tp0 __attribute__((aligned(sizeof(*tp))));
	if(tp0.tv_sec == 0)
		tp0.tv_sec = fe(time,0,0,0,0);

	long long dt = read_tsc();
	tp->tv_sec = tp0.tv_sec + dt/procinfo.tsc_freq;
	tp->tv_usec = (dt % procinfo.tsc_freq)*1000000/procinfo.tsc_freq;

	return 0;
}

