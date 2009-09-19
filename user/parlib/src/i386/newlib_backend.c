/* See COPYRIGHT for copyright information. */

/** @file
 * @brief Backend file for newlib functionality
 *
 * This file is responsible for defining the syscalls needed by newlib to 
 * impliment the newlib library
 * *
 * @author Paul Pearce <pearce@eecs.berkeley.edu>
 * @author Kevin Klues <klueska@cs.berkeley.edu>
 *
 */

#include <parlib.h>
#include <unistd.h>
#include <newlib_backend.h>
#include <string.h>
#include <malloc.h>
#include <stdlib.h>
#include <stdio.h>
#include <debug.h>
#include <sys/times.h>

/* environ
 * A pointer to a list of environment variables and their values. 
 * For a minimal environment, this empty list is adequate.
 */
char *__env[1] = { 0 };
char **environ = __env;

/* _exit()
 * Exit a program without cleaning up files. 
 * If your system doesn't provide this, it is best to avoid linking 
 * with subroutines that require it (exit, system).
 */
void _exit(int __status)
{
	sys_proc_destroy(sys_getpid()); // TODO: can run getpid and cache it
	while(1); //Should never get here...
}
    
/* close()
 * Close a file. 
 */
int close(int file) {
	close_subheader_t msg;
	debug_in_out("CLOSE\n");

	// If trying to close stdin/out/err just return
	if ((file == STDIN_FILENO) || (file == STDERR_FILENO) 
                                   || (file == STDOUT_FILENO))
		return 0;
	
	msg.id = CLOSE_ID;
	msg.fd = file;
	
	// Send message
	response_t *result = send_message(&msg, sizeof(close_subheader_t),CLOSE_ID);

	int return_val;

	if (result == NULL) {
		errno = ECHANNEL;
		return_val = -1;
	} else {
		// Read result
		return_val = result->ret;
		if (return_val == -1) errno = result->err;
		free(result);
	}
	
	return return_val;
}

/* execve()
 * Transfer control to a new process. 
 * Minimal implementation (for a system without processes).
 */

int execve(const char *name, char * const argv[], char * const env[]) 
{
	debug_in_out("EXECVE\n");
	errno = ENOMEM;
	return -1;
}

/* fork()
 * Create a new process. 
 * Minimal implementation (for a system without processes).
 */
pid_t fork(void) 
{
	debug_in_out("FORK\n");
	errno = EAGAIN;
    return -1;
}

/* fstat()
 * Status of an open file. 
 * For consistency with other minimal implementations in these stubs, 
 * all files are regarded as character special devices. 
 * The sys/stat.h msg file required is distributed in the include 
 * subdirectory for the newlib C library.
 */
int fstat(int file, struct stat *st) 
{
	fstat_subheader_t msg;
	debug_in_out("FSTAT\n");	

	// Kevin added this. I believe its needed for the serial branch
	// Not used with the stdout hack. Review for removing stdout hack.
	//st->st_mode = S_IFCHR;
	
	// stdout hack
	if (file == 1) {
		st->st_mode = 8592;
		return 0;
	}
	
	msg.id = FSTAT_ID;
	msg.fd = file;

	// Send message
	response_t *result = send_message(&msg, sizeof(fstat_subheader_t),FSTAT_ID);

	// Read result
	int return_val;

	if (result == NULL) {
		errno = ECHANNEL;
		return_val = -1;
	} else {
		return_val = result->ret;
		if (return_val == -1)
			errno = result->err;
		else
			memcpy(st, (&result->st), sizeof(struct stat));
			
		free(result);
	}

	return return_val;
}

/* getpid()
 * Process-ID; this is sometimes used to generate strings unlikely to 
 * conflict with other processes. Minimal implementation, for a system 
 * without processes.
 */
pid_t getpid(void) 
{
	return sys_getpid(); // TODO: can run getpid and cache it
}

/* isatty()
 * Query whether output stream is a terminal. 
 * For consistency with the other minimal implementations, 
 * which only support output to stdout, this minimal 
 * implementation is suggested.
 */
int isatty(int file) 
{
	isatty_subheader_t msg;
	debug_in_out("ISATTY\n");

	// Cheap hack to avoid sending serial comm for stuff we know
	if ((STDIN_FILENO == file) || (STDOUT_FILENO == file) 
                               || (STDERR_FILENO == file))
		return 1;
	
	msg.id = ISATTY_ID;
	msg.fd = file;

	// Send message
	response_t *result = send_message(&msg, sizeof(isatty_subheader_t),ISATTY_ID);

	int return_val; 

	// Note: Ret val of 0 defined to be an error, not < 0. Go figure.
	if (result == NULL) {
		errno = ECHANNEL;
		return_val = 0;
	} else {
		// Read result
		return_val = result->ret;
		if (return_val == 0) errno = result->err;
		free(result);
	} 
	
	return return_val;
}

/* kill()
 * Send a signal. 
 * Minimal implementation.
 */
int kill(int pid, int sig) 
{
	debug_in_out("KILL\n");
	errno = EINVAL;
    return -1;
}

/* link()
 * Establish a new name for an existing file. 
 * Minimal implementation.
 */
int link(const char *old, const char *new) 
{
	link_subheader_t *msg;
	response_t *result;
	debug_in_out("LINK\n");
	
	int s_len_old = strlen(old) + 1; // Null terminator
	int s_len_new = strlen(new) + 1; // Null terminator

	msg = malloc(sizeof(link_subheader_t) + s_len_old + s_len_new);
	if (msg == NULL)
		return -1;
	
	msg->id = LINK_ID;
	msg->old_len = s_len_old;
	msg->new_len = s_len_new;
	msg->total_len = s_len_old + s_len_new;
	
	memcpy(msg->buf, old, s_len_old - 1);
	msg->buf[s_len_old - 1] = '\0';
	memcpy(msg->buf + s_len_old, new, s_len_new - 1);
	msg->buf[s_len_old + s_len_new - 1] = '\0';

	// Send message
	result =
		send_message((char *CT(sizeof(link_subheader_t)+s_len_old+s_len_new))TC(msg),
	                 sizeof(link_subheader_t) + msg->total_len,LINK_ID);

	free(msg);

	// Read result
	int return_val;

	if (result == NULL) {
		errno = ECHANNEL;
		return_val = -1;
	} else {
		return_val = result->ret;
		if (return_val == -1) errno = result->err;
		free(result);
	}

	return return_val;
}

/* lseek()
 * Set position in a file. 
 * Minimal implementation.
 */
off_t lseek(int file, off_t ptr, int dir) 
{
	lseek_subheader_t msg;
	debug_in_out("LSEEK\n");	
	
	msg.id = LSEEK_ID;
	msg.fd = file;
	msg.ptr = ptr;
	msg.dir = dir;

	// Send message
	response_t *result = send_message(&msg, sizeof(lseek_subheader_t),LSEEK_ID);

	int return_val;

	if (result == NULL) {
		errno = ECHANNEL;
		return_val = -1;
	} else {
		// Read result
		return_val = result->ret;
		if (return_val == -1) errno = result->err;
		free(result);
	}

	return return_val;
}

/* open()
 * Open a file. 
 */
int open(const char *name, int flags, int mode) 
{
	open_subheader_t *msg;
	debug_in_out("OPEN\n");

	int s_len = strlen(name) + 1; // Null terminator

	msg = malloc(sizeof(open_subheader_t) + s_len);
	if (msg == NULL)
		return -1;

	msg->id = OPEN_ID;
	msg->flags = flags;
	msg->mode = mode;
	msg->len = s_len;

	memcpy(msg->buf, name, s_len - 1);
	msg->buf[s_len - 1] = '\0';

	// Send message
	response_t *result =
		send_message((char *CT(sizeof(open_subheader_t)+s_len))TC(msg),
	                 sizeof(open_subheader_t) + s_len,OPEN_ID);

	free(msg);

	// Read result
	int return_val;

	if (result == NULL) {
		errno = ECHANNEL;
		return_val = -1;
	} else {
		return_val = result->ret;
		if (return_val == -1) errno = result->err;
		free(result);
	} 

	return return_val;
}

/* read()
 * Read from a file. 
 */
ssize_t read(int file, void *ptr, size_t len) 
{
	read_subheader_t msg;
	uint8_t *CT(len) _ptr = ptr;
	debug_in_out("READ\n");

	// Console hack.
	if (file == STDIN_FILENO) {
		for(int i=0; i<len; i++)	
			_ptr[i] = (uint8_t)sys_cgetc();
		return len;
	}
	
	msg.id = READ_ID;
	msg.fd = file;
	msg.len = len;
	
	// Send message
	response_t *result = send_message(&msg, sizeof(read_subheader_t),READ_ID);

	// Read result
	int return_val;

	if (result == NULL) {
		errno = ECHANNEL;
		return_val = -1;
	} else {
		return_val = result->ret;
		if (return_val < 0)
			errno = result->err;
		else
			memcpy(_ptr, result->buf, return_val);

		free(result);
	}

	return return_val;
}

/* Read len bytes from the given channel to the buffer.
 * If peek is NO_PEEK, will wait indefinitely until that much data is read.
 * If peek is PEEK, if no data is available, will return immediately.
 *		However once some data is available, 
 *                      will block until the entire amount is available.
 */
int read_from_channel(char * buf, int len, int peek)
{
	// TODO: NEED TO IMPLIMENT A TIMEOUT
	// 			Also, watch out for CONNECTION TERMINATED
	int total_read = 0;

	//int just_read = sys_serial_read(buf, len);
	int just_read = sys_eth_read(buf, len);


	if (just_read < 0) return just_read;
	if (just_read == 0 && peek) return just_read;

	total_read += just_read;

	while (total_read != len) {
		//just_read = sys_serial_read(buf + total_read, len - total_read);
		just_read = sys_eth_read(buf + total_read, len - total_read);
		
		if (just_read == -1) return -1;
		total_read += just_read;
	}

	return total_read;
}

int read_response_from_channel(response_t *response) 
{
	return read_from_channel(	(char*CT(sizeof(response_t))) TC(response), 
               					sizeof(response_t), 
								NO_PEEK);
}

int read_buffer_from_channel(char *buf, int len) 
{
	return read_from_channel(	buf, 
               					len, 
								NO_PEEK);
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
	debug_in_out("SBRK\n");
	debug_in_out("\tincr: %u\n", incr);	

	#define HEAP_SIZE (1<<18)
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
	debug_in_out("\treturning: %u\n", prev_heap_end);
	return (caddr_t) prev_heap_end;
}

/* send_message()
 * Write the message in buffer out on the channel, and wait for a response.
 * Caller is responsible for management of buffer passed in and buffer returned.
 */
response_t *send_message(char *msg, int len, syscall_id_t this_call_id)
{
	if (write_to_channel(msg, len) != len)
		return NULL;

	int ret_value, buffer_size;
	
	response_t temp_response;
	response_t *return_response = NULL;

	// Pull the response from the server out of the channel.
	if (read_response_from_channel(	&temp_response ) == -1) 
		return NULL;

	ret_value = temp_response.ret;
	
	char* buffer = NULL;

	if (this_call_id == READ_ID) 
	{
		if (ret_value < 0)
			buffer_size = 0;
		else
			buffer_size = ret_value;
			
		return_response = malloc(sizeof(response_t) + buffer_size);

		if (return_response == NULL)
			return NULL;
		
		if (read_buffer_from_channel(return_response->buf, buffer_size) == -1)
			return NULL;

	} 
	else 
	{
			
		return_response = malloc(sizeof(response_t));
		
		if (return_response == NULL)
			return NULL;
	}

	*return_response = temp_response;

	return return_response;
}


/* stat()
 * Status of a file (by name). 
 * Minimal implementation.
 */
int stat(const char *file, struct stat *st) 
{
	stat_subheader_t *msg;
	debug_in_out("STAT\n");
	
	int s_len = strlen(file) + 1; // Null terminator

	// Allocate a new buffer of proper size and pack
	msg = malloc(sizeof(stat_subheader_t) + s_len);
	if (msg == NULL)
		return -1;
	
	msg->id = STAT_ID;
	msg->len = s_len;

	memcpy(msg->buf, file, s_len - 1);
	msg->buf[s_len - 1] = '\0';

	// Send message
	response_t *result =
		send_message((char *CT(sizeof(stat_subheader_t) + s_len))TC(msg),
		             sizeof(stat_subheader_t) + s_len, STAT_ID);

	free(msg);

	// Read result
	int return_val;

	if (result == NULL) {
		errno = ECHANNEL;
		return_val = -1;
	} else {
		return_val = result->ret;

		if (return_val == -1)
			errno = result->err;
		else
			memcpy(st, &(result->st), sizeof(struct stat));

		free(result);

	}

	return return_val;
}

/* times()
 * Timing information for current process. 
 * Minimal implementation.
 */
clock_t times(struct tms *buf) 
{
	debug_in_out("TIMES");
	return -1;
}

/* unlink()
 * Remove a file's directory entry. 
 * Minimal implementation.
 */
int unlink(const char *name) 
{
	unlink_subheader_t *msg;
	debug_in_out("UNLINK\n");
	
	int s_len = strlen(name) + 1; // Null terminator

	// Allocate a new buffer of proper size and pack
	msg = malloc(sizeof(unlink_subheader_t) + s_len);
	if (msg == NULL)
		return -1;
	
	msg->id = UNLINK_ID;
	msg->len = s_len;
	
	memcpy(msg->buf, name, s_len - 1);
	msg->buf[s_len - 1] = '\0';

	// Send message
	response_t *result =
		send_message((char *CT(sizeof(unlink_subheader_t) + s_len))TC(msg),
		             sizeof(unlink_subheader_t) + s_len,UNLINK_ID);

	free(msg);

	// Read result
	int return_val;

	if (result == NULL) {
		errno = ECHANNEL;
		return_val = -1;
	} else {
		return_val = result->ret;
		if (return_val == -1) errno = result->err;
		free(result);
	} 
	
	return return_val;
}

/* wait()
 * Wait for a child process. 
 * Minimal implementation.
 */
int wait(int *status) 
{
	debug_in_out("WAIT\n");
	errno = ECHILD;
	return -1;
}

/* write()
 * Write to a file. 
 */
ssize_t write(int file, const void *ptr, size_t len)
{
	write_subheader_t *msg;
	debug_in_out("WRITE\n");	
	debug_in_out("\tFILE: %u\n", file);

	debug_write_check("Writing len: %d\n", len);

	if ((file == STDIN_FILENO) || (file == STDERR_FILENO) 
                                   || (file == STDOUT_FILENO))
		return sys_cputs(ptr, len);
	
	// Allocate a new buffer of proper size and pack
	msg = malloc(sizeof(write_subheader_t) + len);
	if (msg == NULL)
		return -1;
	
	msg->id = WRITE_ID;
	msg->fd = file;
	msg->len = len;

	memcpy(msg->buf, ptr, len);

	// Send message
	response_t *result =
		send_message((char *CT(sizeof(write_subheader_t) + len))TC(msg),
		             sizeof(write_subheader_t) + len,WRITE_ID);

	free(msg);

	// Read result
	int return_val;

	if (result == NULL) {
		errno = ECHANNEL;
		return_val = -1;
	} else {
		return_val = result->ret;
		if (return_val == -1) errno = result->err;
		free(result);
	} 

	return return_val;
}


/* write_to_channel()
 * Send a message out over the channel, defined by msg, of length len
 */
int write_to_channel(char * msg, int len)
{
	//return sys_serial_write((char*)msg, len);
	return sys_eth_write(msg, len);
	
}
