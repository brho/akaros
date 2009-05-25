/* See COPYRIGHT for copyright information. */
/* Kevin Klues <klueska@cs.berkeley.edu>	*/

#ifdef __DEPUTY__
#pragma nodeputy
#endif

#include <inc/lib.h>
#include <libc_wrappers.h>

/* environ
 * A pointer to a list of environment variables and their values. 
 * For a minimal environment, this empty list is adequate.
 */
char *__env[1] = { 0 };
char **environ = __env;
extern env_t* env;

/* _exit()
 * Exit a program without cleaning up files. 
 * If your system doesn't provide this, it is best to avoid linking 
 * with subroutines that require it (exit, system).
 */
void _exit() 
{
	sys_env_destroy(env->env_id);
}
    
/* close()
 * Close a file. 
 * Minimal implementation.
 */
int close(int file) 
{
	return -1;
}

/* execve()
 * Transfer control to a new process. 
 * Minimal implementation (for a system without processes).
 */

int execve(char *name, char **argv, char **env) 
{
	errno = ENOMEM;
	return -1;
}

/* fork()
 * Create a new process. 
 * Minimal implementation (for a system without processes).
 */
int fork(void) 
{
	errno = EAGAIN;
    return -1;
}

/* fstat()
 * Status of an open file. 
 * For consistency with other minimal implementations in these stubs, 
 * all files are regarded as character special devices. 
 * The sys/stat.h header file required is distributed in the include 
 * subdirectory for the newlib C library.
 */
int fstat(int file, struct stat *st) 
{
	st->st_mode = S_IFCHR;
	return 0;
}

/* getpid()
 * Process-ID; this is sometimes used to generate strings unlikely to 
 * conflict with other processes. Minimal implementation, for a system 
 * without processes.
 */
int getpid(void) 
{
	return env->env_id;
}

/* isatty()
 * Query whether output stream is a terminal. 
 * For consistency with the other minimal implementations, 
 * which only support output to stdout, this minimal 
 * implementation is suggested.
 */
int isatty(int file) 
{
	return 1;
}

/* kill()
 * Send a signal. 
 * Minimal implementation.
 */
int kill(int pid, int sig) 
{
	errno = EINVAL;
    return -1;
}

/* link()
 * Establish a new name for an existing file. 
 * Minimal implementation.
 */
int link(char *old, char *new) 
{
	errno = EMLINK;
	return -1;
}

/* lseek()
 * Set position in a file. 
 * Minimal implementation.
 */
int lseek(int file, int ptr, int dir) 
{
	return 0;
}

/* __sseek64()
 * Set position in a file. 
 * Minimal implementation.
 */
int __sseek64(int file, int ptr, int dir) 
{
	return 0;
}

/* open()
 * Open a file. 
 * Minimal implementation.
 */
int open(const char *name, int flags, int mode) 
{
	return -1;
}

/* read()
 * Read from a file. 
 * Minimal implementation.
 */
int read(int file, char *ptr, int len) 
{
	return 0;
}

/* sbrk()
 * Increase program data space. 
 * As malloc and related functions depend on this, it is 
 * useful to have a working implementation. 
 * The following suffices for a standalone system; it exploits the 
 * symbol _end automatically defined by the GNU linker.
 */
caddr_t sbrk(int incr) 
{
/*
	extern char _end;		// Defined by the linker
	static char *heap_end;
	char *prev_heap_end;

	if (heap_end == 0) {
		heap_end = &_end;
	}
	prev_heap_end = heap_end;
	if (heap_end + incr > stack_ptr) {
		write (1, "Heap and stack collision\n", 25);
		abort ();
	}
      
	heap_end += incr;
	return (caddr_t) prev_heap_end;
*/
	errno = ENOMEM;
	return (void*)-1;
}

/* stat()
 * Status of a file (by name). 
 * Minimal implementation.
 */
int stat(char *file, struct stat *st) 
{
	st->st_mode = S_IFCHR;
	return 0;
}

/* times()
 * Timing information for current process. 
 * Minimal implementation.
 */
int times(struct tms *buf) 
{
	return -1;
}

/* unlink()
 * Remove a file's directory entry. 
 * Minimal implementation.
 */
int unlink(char *name) 
{
	errno = ENOENT;
	return -1;
}

/* wait()
 * Wait for a child process. 
 * Minimal implementation.
 */
int wait(int *status) 
{
	errno = ECHILD;
	return -1;
}

/* write()
 * Write to a file. 
 * libc subroutines will use this system routine for output 
 * to all files, including stdout—so if you need to generate 
 * any output, for example to a serial port for debugging, 
 * you should make your minimal write capable of doing this. 
 * The following minimal implementation is an incomplete example; 
 * it relies on a outbyte subroutine (not shown; typically, you must 
 * write this in assembler from examples provided by your hardware 
 * manufacturer) to actually perform the output.
 */
#define outbyte(arg)
int write(int file, char *ptr, int len) {
	return 0;
}

/* __swrite64()
 * Write to a file. 
 */
int __swrite64(int file, char *ptr, int len) {
	return 0;
}
