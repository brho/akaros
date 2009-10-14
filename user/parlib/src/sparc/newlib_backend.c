/* See COPYRIGHT for copyright information. */
/* Andrew Waterman <waterman@eecs.bekeley.edu> */

#include <arch/frontend.h>
#include <parlib.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include <sys/times.h>
#include <sys/time.h>
#include <debug.h>

char *__env[1] = { 0 };
char **environ = __env;

#define IS_CONSOLE(fd) ((uint32_t)(fd) < 3)

int
getpid(void)
{
	static int pid = 0;
	if(pid == 0)
		return pid = sys_getpid();

	return pid;
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
	return IS_CONSOLE(fd);
}

int
fork(void)
{
	return -1;
}

int
execve(const char* name, char* const argv[], char* const env[])
{
	return -1;
}

int
kill(int pid, int sig)
{
	return -1;
}

int
wait(int* status)
{
	return -1;
}

int
link(const char *old, const char *new)
{
	return -1;
}

int
unlink(const char* old)
{
	return syscall(SYS_frontend,RAMP_SYSCALL_unlink,(int)old,0,0,0);
}

int
fstat(int fd, struct stat* st)
{
	return syscall(SYS_frontend,RAMP_SYSCALL_fstat,fd,(int)st,0,0);
}

int
stat(const char* name, struct stat* st)
{
	return syscall(SYS_frontend,RAMP_SYSCALL_stat,(int)name,(int)st,0,0);
}

off_t
lseek(int fd, off_t ptr, int dir)
{
	return syscall(SYS_frontend,RAMP_SYSCALL_lseek,fd,ptr,dir,0);
}

ssize_t
write(int fd, const void* ptr, size_t len)
{
	return syscall(SYS_frontend,RAMP_SYSCALL_write,fd,(int)ptr,len,0);
}

ssize_t
read(int fd, void* ptr, size_t len)
{
	return syscall(SYS_frontend,RAMP_SYSCALL_read,fd,(int)ptr,len,0);
}

int
open(char* name, int flags, int mode)
{
	return syscall(SYS_frontend,RAMP_SYSCALL_open,(int)name,flags,mode,0);
}

int
close(int fd)
{
	if(IS_CONSOLE(fd))
		return 0;
	return syscall(SYS_frontend,RAMP_SYSCALL_close,fd,0,0,0);
}

clock_t
times(struct tms* buf)
{
	return -1;
}

int
gettimeofday(struct timeval* tp, void* tzp)
{
	return -1;
}

/* sbrk()
 * Increase program data space. 
 * As malloc and related functions depend on this, it is 
 * useful to have a working implementation. 
 * The following suffices for a standalone system; it exploits the 
 * symbol _end automatically defined by the GNU linker.
 */
void* sbrk(ptrdiff_t incr) 
{
	#define HEAP_SIZE (1<<23)
	static uint8_t array[HEAP_SIZE];
	static uint8_t *BND(array, array + HEAP_SIZE) heap_end = array;
	static uint8_t *stack_ptr = &(array[HEAP_SIZE-1]);

	uint8_t* prev_heap_end; 

	prev_heap_end = heap_end;
	if (heap_end + incr > stack_ptr) {
		errno = ENOMEM;
		return (void*CT(1))TC(-1);
	}

	heap_end += incr;
	return (caddr_t) prev_heap_end;
}
