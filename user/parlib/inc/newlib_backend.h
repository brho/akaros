/* See COPYRIGHT for copyright information. */
/* Kevin Klues <klueska@cs.berkeley.edu>	*/

#ifndef _NEWLIB_LIBC_WRAPPERS_H_
#define _NEWLIB_LIBC_WRAPPERS_H_

#include <errno.h>
#include <sys/stat.h>



#define debug_in_out(...) // debug(__VA_ARGS__)  
#define debug_write_check(fmt, ...)  //debug(fmt, __VA_ARGS__)

typedef uint32_t syscall_id_t;

// ALL STRUCTS MUST BE PADDED TO THE SAME SIZE.
// Basically, to make IVY annotation possible with as few TC's as possible
// We do some ugly things with unions, which results in us needing this padding
typedef struct open_subheader {
	syscall_id_t id;
	uint32_t flags;
	uint32_t mode;
	uint32_t len;
	char (CT(len) buf)[0];
} open_subheader_t;

typedef struct close_subheader {
	syscall_id_t id;
	uint32_t fd;
	uint32_t FILL1;
	uint32_t FILL2;
} close_subheader_t;

typedef struct read_subheader {
	syscall_id_t id;
	uint32_t fd;
	uint32_t len;
	uint32_t FILL1;
} read_subheader_t;

typedef struct write_subheader {
	syscall_id_t id;
	uint32_t fd;
	uint32_t len;
	uint32_t FILL1;
	char (CT(len) buf)[0];
} write_subheader_t;

typedef struct lseek_subheader {
	syscall_id_t id;
	uint32_t fd;
	uint32_t ptr;
	uint32_t dir;
} lseek_subheader_t;

typedef struct isatty_subheader {
	syscall_id_t id;
	uint32_t fd;
	uint32_t FILL1;
	uint32_t FILL2;
} isatty_subheader_t;

typedef struct link_subheader {
	syscall_id_t id;
	uint32_t old_len;
	uint32_t new_len;
	uint32_t total_len;
	char (CT(total_len) buf)[0];
} link_subheader_t;

typedef struct unlink_subheader {
	syscall_id_t id;
	uint32_t len;
	uint32_t FILL1;
	uint32_t FILL2;
	char (CT(len) buf)[0];
} unlink_subheader_t;

typedef struct fstat_subheader {
	syscall_id_t id;
	uint32_t fd;
	uint32_t FILL1;
	uint32_t FILL2;
} fstat_subheader_t;

typedef struct stat_subheader {
	syscall_id_t id;
	uint32_t len;
	uint32_t FILL1;
	uint32_t FILL2;
	char (CT(len) buf)[0];
} stat_subheader_t;

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

#if 0

zra: this sort of thing is more useful if the C code is receiving a message.

typedef struct backend_msg {
	syscall_id_t id;
	union {
		open_subheader_t open WHEN(id == OPEN_ID);
		close_subheader_t close WHEN(id == CLOSE_ID);
		read_subheader_t read WHEN(id == READ_ID);
		write_subheader_t write WHEN(id == WRITE_ID);
		lseek_subheader_t lseek WHEN(id == LSEEK_ID);
		isatty_subheader_t isatty WHEN(id == ISATTY_ID);
		link_subheader_t link WHEN(id == LINK_ID);
		unlink_subheader_t unlink WHEN(id == UNLINK_ID);
		fstat_subheader_t fstat WHEN(id == FSTAT_ID);
		stat_subheader_t stat WHEN(id == STAT_ID);	
	};
} msg_t;
#endif

typedef struct response {
	int32_t ret;
	uint32_t err;
	struct stat st;
	char (CT(ret) buf)[0];
} response_t;


// New errno we want to define if a channel error occurs
// Not yet fully implimented
#define ECHANNEL -999

// Value to send across the channel as a function return value in the event of server side termination
// Note yet fully implimented
#define CONNECTION_TERMINATED -2

// Macros for the read_from_channel function
#define PEEK    1
#define NO_PEEK 0


/* Read len bytes from the given channel to the buffer.
 * If peek is NO_PEEK, will wait indefinitely until that much data is read.
 * If peek is PEEK, if no data is available, will return immediately.
 *              However once some data is available,
 *                      will block until the entire amount is available.
 */
int read_from_channel(char *CT(len) buf, int len, int peek);

int read_response_from_channel(response_t *CT(1) response);
int read_buffer_from_channel(char *CT(len) buf, int len);


/* send_message()
 * Write the message defined in buffer out across the channel, and wait for a response.
 * Caller is responsible for management of both the buffer passed in and the buffer ptr returned.
 */
response_t *send_message(char *COUNT(len) msg, int len, syscall_id_t id);

/* write_to_channel()
 * Send a message out over the channel, defined by msg, of length len
 */
int write_to_channel(char *COUNT(len) msg, int len);

#endif //_NEWLIB_LIBC_WRAPPERS_H_

