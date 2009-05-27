/* See COPYRIGHT for copyright information. */
/* Kevin Klues <klueska@cs.berkeley.edu>	*/

#ifndef _NEWLIB_LIBC_WRAPPERS_H
#define _NEWLIB_LIBC_WRAPPERS_H_

#include <errno.h>
#include <sys/stat.h>
#undef errno
extern int errno;

#define OPEN_ID		0
#define CLOSE_ID	1
#define READ_ID		2
#define WRITE_ID	3
#define LINK_ID		4
#define UNLINK_ID	5
#define LSEEK_ID	6
#define FSTAT_ID	7
#define ISATTY_ID	8
#define STAT_ID		9
#define NUM_CALLS	10


// Fixed size of the client->server msgs for the various calls.
#define OPEN_MESSAGE_FIXED_SIZE 	sizeof(syscall_id) + 3*sizeof(int)
#define CLOSE_MESSAGE_FIXED_SIZE 	sizeof(syscall_id) + sizeof(int)
#define READ_MESSAGE_FIXED_SIZE 	sizeof(syscall_id) + 2*sizeof(int)
#define WRITE_MESSAGE_FIXED_SIZE 	sizeof(syscall_id) + 2*sizeof(int)
#define LSEEK_MESSAGE_FIXED_SIZE 	sizeof(syscall_id) + 3*sizeof(int)
#define ISATTY_MESSAGE_FIXED_SIZE 	sizeof(syscall_id) + sizeof(int)
#define LINK_MESSAGE_FIXED_SIZE 	sizeof(syscall_id) + 2*sizeof(int)
#define UNLINK_MESSAGE_FIXED_SIZE 	sizeof(syscall_id) + sizeof(int)
#define FSTAT_MESSAGE_FIXED_SIZE 	sizeof(syscall_id) + sizeof(int)
#define STAT_MESSAGE_FIXED_SIZE 	sizeof(syscall_id) + sizeof(int)

// What is the max number of arguments (besides the syscall_id) we can have.
// This should be the max of the above sizes.
// This exists so we can  allocate a fixed amount of memory to process all incoming msgs
// TODO: This makes the implicit assumption when referenced in server.c that each argument is of type int.
//		If we change the above defs to no longer be sizeof(int) this will break in server.c
#define MAX_FIXED_ARG_COUNT 3

// Fixed server-> respponse msg sizes.
#define OPEN_RETURN_MESSAGE_FIXED_SIZE 		sizeof(int)
#define CLOSE_RETURN_MESSAGE_FIXED_SIZE 	sizeof(int)
#define READ_RETURN_MESSAGE_FIXED_SIZE 		sizeof(int)
#define WRITE_RETURN_MESSAGE_FIXED_SIZE 	sizeof(int)
#define LSEEK_RETURN_MESSAGE_FIXED_SIZE 	sizeof(int)
#define ISATTY_RETURN_MESSAGE_FIXED_SIZE 	sizeof(int)
#define UNLINK_RETURN_MESSAGE_FIXED_SIZE 	sizeof(int)
#define LINK_RETURN_MESSAGE_FIXED_SIZE 		sizeof(int)
#define STAT_RETURN_MESSAGE_FIXED_SIZE 		sizeof(int) + sizeof(struct stat)
#define FSTAT_RETURN_MESSAGE_FIXED_SIZE 	sizeof(int) + sizeof(struct stat)

// New errno we want to define if a channel error occurs
// Not yet fully implimented
#define ECHANNEL -999

// Value to send across the channel as a function return value in the event of server side termination
// Note yet fully implimented
#define CONNECTION_TERMINATED -2

// Should refactor this next typedef. Just leave it as type int.
typedef int syscall_id;

// Replace with uint8?
typedef char byte;

/* Read len bytes from the given channel to the buffer.
 * If peek is 0, will wait indefinitely until that much data is read.
 * If peek is 1, if no data is available, will return immediately.
 *		However once some data is available, it will block until the entire amount is available.
 */
int read_from_channel(byte * buf, int len, int peek);

/* send_message()
 * Write the message defined in buffer out across the channel, and wait for a response.
 * Caller is responsible for management of both the buffer passed in and the buffer ptr returned.
 */
byte *send_message(byte *message, int len);

/* write_to_channel()
 * Send a message out over the channel, defined by msg, of length len
 */
int write_to_channel(byte * msg, int len);

/* _exit()
 * Exit a program without cleaning up files. 
 * If your system doesn't provide this, it is best to avoid linking 
 * with subroutines that require it (exit, system).
 */
void _exit(int __status);
    
/* close()
 * Close a file. 
 */
int close(int file);

/* execve()
 * Transfer control to a new process. 
 * Minimal implementation (for a system without processes).
 */

int execve(char *name, char **argv, char **env);

/* fork()
 * Create a new process. 
 * Minimal implementation (for a system without processes).
 */
int fork(void);

/* fstat()
 * Status of an open file. 
 * For consistency with other minimal implementations in these stubs, 
 * all files are regarded as character special devices. 
 * The sys/stat.h header file required is distributed in the include 
 * subdirectory for the newlib C library.
 */
int fstat(int file, struct stat *st);

/* getpid()
 * Process-ID; this is sometimes used to generate strings unlikely to 
 * conflict with other processes. Minimal implementation, for a system 
 * without processes.
 */
int getpid(void); 

/* isatty()
 * Query whether output stream is a terminal. 
 * For consistency with the other minimal implementations, 
 * which only support output to stdout, this minimal 
 * implementation is suggested.
 */
int isatty(int file) ;

/* kill()
 * Send a signal. 
 * Minimal implementation.
 */
int kill(int pid, int sig); 

/* link()
 * Establish a new name for an existing file. 
 * Minimal implementation.
 */
int link(char *old, char *new);

/* lseek()
 * Set position in a file. 
 * Minimal implementation.
 */
int lseek(int file, int ptr, int dir); 

/* open()
 * Open a file. 
 */
int open(const char *name, int flags, int mode); 

/* read()
 * Read from a file. 
 */
int read(int file, char *ptr, int len); 

/* sbrk()
 * Increase program data space. 
 * As malloc and related functions depend on this, it is 
 * useful to have a working implementation. 
 * The following suffices for a standalone system; it exploits the 
 * symbol _end automatically defined by the GNU linker.
 */
caddr_t sbrk(int incr); 

/* stat()
 * Status of a file (by name). 
 * Minimal implementation.
 */
int stat(char *file, struct stat *st);

/* times()
 * Timing information for current process. 
 * Minimal implementation.
 */
int times(struct tms *buf); 

/* unlink()
 * Remove a file's directory entry. 
 * Minimal implementation.
 */
int unlink(char *name); 

/* wait()
 * Wait for a child process. 
 * Minimal implementation.
 */
int wait(int *status); 

/* write()
 * Write to a file. 
 */
int write(int file, char *ptr, int len);

#endif //_NEWLIB_LIBC_WRAPPERS_H_

