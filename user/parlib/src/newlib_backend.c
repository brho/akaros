/* See COPYRIGHT for copyright information. */
/* Kevin Klues <klueska@cs.berkeley.edu>	*/

#ifdef __DEPUTY__
#pragma nodeputy
#endif

#include <parlib.h>
#include <newlib_backend.h>
#include <string.h>
#include <malloc.h>
#include <stdlib.h>
#include <stdio.h>

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
void _exit(int __status) _ATTRIBUTE ((noreturn))
{
	sys_env_destroy(env->env_id);
	while(1); //Should never get here...
}
    
/* close()
 * Close a file. 
 */
int close(int file) 
{
	// Allocate a new buffer of proper size
	byte *out_msg = malloc(CLOSE_MESSAGE_FIXED_SIZE);
	if(out_msg == NULL)
		return -1;

	byte *out_msg_pos = out_msg;

	// Fill the buffer
	*((syscall_id *)out_msg_pos) = CLOSE_ID;
	out_msg_pos += sizeof(syscall_id);

	*((int*)out_msg_pos) = file;
	out_msg_pos += sizeof(int);


	// Send message
	byte *result = send_message(out_msg, CLOSE_MESSAGE_FIXED_SIZE);

	free(out_msg);

	if (result != NULL) {
		// Read result
		int return_val;
		return_val = *((int *) result);
		free(result);
		return return_val;
	} else {
		return -1;
	}
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

/* open()
 * Open a file. 
 */
int open(const char *name, int flags, int mode) 
{
	int s_len = strlen(name) + 1; // Null terminator
	int out_msg_len = OPEN_MESSAGE_FIXED_SIZE + s_len;

	// Allocate a new buffer of proper size
	byte *out_msg = malloc(out_msg_len);
	byte *out_msg_pos = out_msg;

	if (out_msg == NULL)
		return -1;

	// Fill the buffer
	*((syscall_id*)out_msg_pos) = OPEN_ID;
	out_msg_pos += sizeof(syscall_id);

	*((int*)out_msg_pos) = flags;
	out_msg_pos += sizeof(int);

	*((int*)out_msg_pos) = mode;
	out_msg_pos += sizeof(int);

	*((int*)out_msg_pos) = s_len;
	out_msg_pos += sizeof(int);

	memcpy(out_msg_pos, name, s_len);

	// Send message
	byte *result = send_message(out_msg, out_msg_len);

	free(out_msg);

	// Read result
	int return_val;

	if (result != NULL) {
		return_val = *((int *)result);
		free(result);
	} else {
		return_val = -1;
	}

	return return_val;
}

/* read()
 * Read from a file. 
 */
int read(int file, char *ptr, int len) 
{
	// Allocate a new buffer of proper size
	byte *out_msg = (byte*)malloc(READ_MESSAGE_FIXED_SIZE);
	if (out_msg == NULL)
		return -1;

	byte *out_msg_pos = out_msg;

	// Fill the buffer
	*((syscall_id*)out_msg_pos) = READ_ID;
	out_msg_pos += sizeof(syscall_id);

	*((int *)out_msg_pos) = file;
	out_msg_pos += sizeof(int);

	*((int *)out_msg_pos) = len;
	out_msg_pos += sizeof(int);

	// Send message
	byte *result = send_message(out_msg, READ_MESSAGE_FIXED_SIZE);

	free(out_msg);

	// Read result
	int return_val;

	if (result != NULL) {
		return_val = *((int *)result);
		memcpy(ptr, ((int *)result) + 1, return_val);
		free(result);
	} else {
		return_val = -1;
	}

	return return_val;
}

/* Read len bytes from the given channel to the buffer.
 * If peek is 0, will wait indefinitely until that much data is read.
 * If peek is 1, if no data is available, will return immediately.
 *		However once some data is available, it will block until the entire amount is available.
 */
int read_from_channel(byte * buf, int len, int peek)
{
	// TODO: NEED TO IMPLIMENT A TIMEOUT
	// 			Also, watch out for CONNECTION TERMINATED
	// TODO: Add a #define for peek / no peek. Don't know why I didnt do this from the start?
	int total_read = 0;

	int just_read = sys_serial_read(buf, len);


	if (just_read < 0) return just_read;
	if (just_read == 0 && peek) return just_read;

	total_read += just_read;

	while (total_read != len) {
		just_read = sys_serial_read(buf + total_read, len - total_read);

		if (just_read == -1) return -1;
		total_read += just_read;
	}

	return total_read;
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
	#define HEAP_SIZE 1000
	static uint8_t array[HEAP_SIZE];
	static uint8_t* heap_end = array;
	static uint8_t* stack_ptr = &(array[HEAP_SIZE-1]);

	uint8_t* prev_heap_end; 

	prev_heap_end = heap_end;
	if (heap_end + incr > stack_ptr) {
		errno = ENOMEM;
		return (void*)-1;
	}
      
	heap_end += incr;
	return (caddr_t) prev_heap_end;
}

/* send_message()
 * Write the message defined in buffer out across the channel, and wait for a response.
 * Caller is responsible for management of both the buffer passed in and the buffer ptr returned.
 */
byte *send_message(byte *message, int len)
{

	syscall_id this_call_id = *((syscall_id*)message);

	if (write_to_channel(message, len) != len)
		return NULL;

	int response_value;

	// Pull the response from the server out of the channel.
	if (read_from_channel((char*)&response_value, sizeof(int), 0) == -1) return NULL;

	byte* return_msg = NULL;

	// TODO: Make these sizes an array we index into, and only have this code once.
	// TODO: Will have a flag that tells us we have a variable length response (right now only for read case)
	// TODO: Default clause with error handling.
	switch (this_call_id) {
		case OPEN_ID:
			if ((return_msg = (byte*)malloc(OPEN_RETURN_MESSAGE_FIXED_SIZE)) == NULL)
				return NULL;

			break;

		case CLOSE_ID:
			if ((return_msg = (byte*)malloc(CLOSE_RETURN_MESSAGE_FIXED_SIZE)) == NULL)
				return NULL;

			break;

		case READ_ID:
			if ((return_msg = (byte*)malloc(READ_RETURN_MESSAGE_FIXED_SIZE + response_value)) == NULL)
				return NULL;


			if ((read_from_channel(return_msg + sizeof(int), response_value, 0)) == -1)
				return NULL;

			break;

		case WRITE_ID:
			if ((return_msg = (byte*)malloc(WRITE_RETURN_MESSAGE_FIXED_SIZE)) == NULL)
				return NULL;

			break;


		case LSEEK_ID:
			if ((return_msg = (byte*)malloc(LSEEK_RETURN_MESSAGE_FIXED_SIZE)) == NULL)
				return NULL;

			break;

		case ISATTY_ID:
			if ((return_msg = (byte*)malloc(ISATTY_RETURN_MESSAGE_FIXED_SIZE)) == NULL)
				return NULL;

			break;

		case UNLINK_ID:
			if ((return_msg = (byte*)malloc(UNLINK_RETURN_MESSAGE_FIXED_SIZE + ((response_value != -1) ? 0 : sizeof(int)))) == NULL)
				return NULL;


			if ((response_value == -1) && ((read_from_channel(return_msg + sizeof(int), sizeof(int), 0)) == -1))
				return NULL;

			break;

		case LINK_ID:
			if ((return_msg = (byte*)malloc(LINK_RETURN_MESSAGE_FIXED_SIZE + ((response_value != -1) ? 0 : sizeof(int)))) == NULL)
				return NULL;


			if ((response_value == -1) && ((read_from_channel(return_msg + sizeof(int), sizeof(int), 0)) == -1))
				return NULL;

			break;

		case FSTAT_ID:
			if ((return_msg = (byte*)malloc(FSTAT_RETURN_MESSAGE_FIXED_SIZE + ((response_value != -1) ? 0 : sizeof(int)))) == NULL)
				return NULL;

			if ((read_from_channel(return_msg + sizeof(int), sizeof(struct stat), 0)) == -1)
				return NULL;

			if ((response_value == -1) && ((read_from_channel(return_msg + sizeof(int) + sizeof(struct stat), sizeof(int), 0)) == -1))
				return NULL;

			break;

		case STAT_ID:
			if ((return_msg = (byte*)malloc(STAT_RETURN_MESSAGE_FIXED_SIZE + ((response_value != -1) ? 0 : sizeof(int)))) == NULL)
				return NULL;

			if ((read_from_channel(return_msg + sizeof(int), sizeof(struct stat), 0)) == -1)
				return NULL;

			if ((response_value == -1) && ((read_from_channel(return_msg + sizeof(int) + sizeof(struct stat), sizeof(int), 0)) == -1))
				return NULL;

			break;
	}

	memcpy(return_msg, &response_value, sizeof(int));

	return return_msg;
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
 */
int write(int file, char *ptr, int len) {
	if(file == 1) //STDOUT_FILENO
		return sys_cputs(ptr, len);
	
	int out_msg_len = WRITE_MESSAGE_FIXED_SIZE + len;

	// Allocate a new buffer of proper size
	byte *out_msg = malloc(out_msg_len);
	byte *out_msg_pos = out_msg;

	// Fill the buffer
	*((syscall_id*)out_msg_pos) = WRITE_ID;
	out_msg_pos += sizeof(syscall_id);

	*((int*)out_msg_pos) = file;
	out_msg_pos += sizeof(int);

	*((int*)out_msg_pos) = len;
	out_msg_pos += sizeof(int);

	memcpy(out_msg_pos, ptr, len);

	// Send message
	byte *result = send_message(out_msg, out_msg_len);

	free(out_msg);

	// Read result
	int return_val;

	if (result != NULL) {
		return_val = *((int *)result);
		free(result);
	} else {
		return_val = -1;
	}

	return return_val;
}


/* write_to_channel()
 * Send a message out over the channel, defined by msg, of length len
 */
int write_to_channel(byte * msg, int len)
{
	return sys_serial_write((char*)msg, len);
}

