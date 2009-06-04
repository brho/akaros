/* See COPYRIGHT for copyright information. */
/* Kevin Klues <klueska@cs.berkeley.edu>	*/

#ifndef _NEWLIB_LIBC_WRAPPERS_H
#define _NEWLIB_LIBC_WRAPPERS_H_

#include <errno.h>
#include <sys/stat.h>

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
#define OPEN_MESSAGE_FIXED_SIZE       sizeof(syscall_id_t) + 3*sizeof(int)
#define CLOSE_MESSAGE_FIXED_SIZE      sizeof(syscall_id_t) + sizeof(int)
#define READ_MESSAGE_FIXED_SIZE       sizeof(syscall_id_t) + 2*sizeof(int)
#define WRITE_MESSAGE_FIXED_SIZE      sizeof(syscall_id_t) + 2*sizeof(int)
#define LSEEK_MESSAGE_FIXED_SIZE      sizeof(syscall_id_t) + 3*sizeof(int)
#define ISATTY_MESSAGE_FIXED_SIZE     sizeof(syscall_id_t) + sizeof(int)
#define LINK_MESSAGE_FIXED_SIZE       sizeof(syscall_id_t) + 2*sizeof(int)
#define UNLINK_MESSAGE_FIXED_SIZE     sizeof(syscall_id_t) + sizeof(int)
#define FSTAT_MESSAGE_FIXED_SIZE      sizeof(syscall_id_t) + sizeof(int)
#define STAT_MESSAGE_FIXED_SIZE       sizeof(syscall_id_t) + sizeof(int)

// What is the max number of arguments (besides the syscall_id_t) we can have.
// This should be the max of the above sizes.
// This exists so we can  allocate a fixed amount of memory to process all incoming msgs
// TODO: This makes the implicit assumption when referenced in server.c that each argument is of type int.
//		If we change the above defs to no longer be sizeof(int) this will break in server.c
#define MAX_FIXED_ARG_COUNT 3

// New errno we want to define if a channel error occurs
// Not yet fully implimented
#define ECHANNEL -999

// Value to send across the channel as a function return value in the event of server side termination
// Note yet fully implimented
#define CONNECTION_TERMINATED -2

// Macros for the read_from_channel function
#define PEEK    1
#define NO_PEEK 0

typedef uint32_t syscall_id_t;

/* Read len bytes from the given channel to the buffer.
 * If peek is NO_PEEK, will wait indefinitely until that much data is read.
 * If peek is PEEK, if no data is available, will return immediately.
 *              However once some data is available,
 *                      will block until the entire amount is available.
 */
int read_from_channel(char * buf, int len, int peek);

/* send_message()
 * Write the message defined in buffer out across the channel, and wait for a response.
 * Caller is responsible for management of both the buffer passed in and the buffer ptr returned.
 */
char *send_message(char *message, int len);

/* write_to_channel()
 * Send a message out over the channel, defined by msg, of length len
 */
int write_to_channel(char *msg, int len);

#endif //_NEWLIB_LIBC_WRAPPERS_H_

