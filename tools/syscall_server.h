#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>

// Todo: review the laundry list of includes.

// Backlog for the listen() call.
#define BACKLOG_LEN 16

#define LISTEN_PORT 12345
#define SERVER_IP "128.32.35.43"

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

// New errno we want to define if a channel error occurs
// Not yet fully implimented
#define ECHANNEL -999

// Value to send across the channel as a function return value in the event of server side termination
// Note yet fully implimented
#define CONNECTION_TERMINATED -2

// Macros for the read_from_channel function
#define PEEK    1
#define NO_PEEK 0

// Should refactor this next typedef. Just leave it as type int.
typedef int syscall_id_t;

// ALL STRUCTS MUST BE PADDED TO THE SAME SIZE.
// Basically, to make IVY annotation possible with as few TC's as possible
// We do some ugly things with unions, which results in us needing this padding
typedef struct open_subheader {
	uint32_t flags;
	uint32_t mode;
	uint32_t len;
	char buf[0];
} open_subheader_t;

typedef struct close_subheader {
	uint32_t fd;
	uint32_t FILL1;
	uint32_t FILL2;
} close_subheader_t;

typedef struct read_subheader {
	uint32_t fd;
	uint32_t len;
	uint32_t FILL1;
} read_subheader_t;

typedef struct write_subheader {
	uint32_t fd;
	uint32_t len;
	uint32_t FILL1;
	char buf[0];
} write_subheader_t;

typedef struct lseek_subheader {
	uint32_t fd;
	uint32_t ptr;
	uint32_t dir;
} lseek_subheader_t;

typedef struct isatty_subheader {
	uint32_t fd;
	uint32_t FILL1;
	uint32_t FILL2;
} isatty_subheader_t;

typedef struct link_subheader {
	uint32_t old_len;
	uint32_t new_len;
	uint32_t FILL1;
	char buf[0];
} link_subheader_t;

typedef struct unlink_subheader {
	uint32_t len;
	uint32_t FILL1;
	uint32_t FILL2;
	char buf[0];
} unlink_subheader_t;

typedef struct fstat_subheader {
	uint32_t fd;
	uint32_t FILL1;
	uint32_t FILL2;
} fstat_subheader_t;

typedef struct stat_subheader {
	uint32_t len;
	uint32_t FILL1;
	uint32_t FILL2;
	char buf[0];
} stat_subheader_t;

typedef struct backend_msg {
	syscall_id_t id;
	union {
		open_subheader_t open;
		close_subheader_t close;
		read_subheader_t read;
		write_subheader_t write;
		lseek_subheader_t lseek;
		isatty_subheader_t isatty;
		link_subheader_t link;
		unlink_subheader_t unlink;
		fstat_subheader_t fstat;
		stat_subheader_t stat;	
	} subheader;
} msg_t;

typedef struct response {
	int32_t ret;
	uint32_t err;
	struct stat st;
	char buf[0];
} response_t;

#undef errno
extern int errno;

response_t* handle_open(msg_t * msg);
response_t* handle_close(msg_t * msg);
response_t* handle_read(msg_t * msg);
response_t* handle_write(msg_t * msg);
response_t* handle_lseek(msg_t * msg);
response_t* handle_isatty(msg_t * msg);
response_t* handle_unlink(msg_t * msg);
response_t* handle_link(msg_t * msg);
response_t* handle_stat(msg_t * msg);
response_t* handle_fstat(msg_t * msg);

int listen_for_connections();
int setup_data_connection(int data_port);
void process_connections(int fd);
int read_from_socket(char* buf, int socket, int len, int peek);
int read_header_from_socket(msg_t* msg, int socket);
int read_buffer_from_socket(char* buf, int socket, int len);
void send_error(int fd);
