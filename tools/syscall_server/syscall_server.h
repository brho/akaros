#ifndef SERIAL_SERVER_H
#define SERIAL_SERVER_H

#include <stdint.h>
#include <sys/stat.h>
#include <newlib_trans.h>

#define SANDBOX_DIR "sandbox/"

#define OPEN_ID			0
#define CLOSE_ID		1
#define READ_ID			2
#define WRITE_ID		3
#define LINK_ID			4
#define UNLINK_ID		5
#define LSEEK_ID		6
#define FSTAT_ID		7
#define ISATTY_ID		8
#define STAT_ID			9
#define NUM_SYSCALLS	10

typedef uint32_t syscall_id_t;

typedef struct open_subheader {
	uint32_t flags;
	uint32_t mode;
	uint32_t len;
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
} link_subheader_t;

typedef struct unlink_subheader {
	uint32_t len;
	uint32_t FILL1;
	uint32_t FILL2;
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
} stat_subheader_t;

typedef struct syscall_req_header {
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
} syscall_req_header_t;

typedef struct syscall_req {
	syscall_req_header_t header;
	uint32_t payload_len;
	void* payload;
} syscall_req_t;

typedef struct syscall_rsp_header {
	int32_t return_val;
	uint32_t return_errno;
} syscall_rsp_header_t;

typedef struct syscall_rsp {
	syscall_rsp_header_t header;
	uint32_t payload_len;
	void* payload;
} syscall_rsp_t;

void run_server();
int init_syscall_server(int* fd_read, int* fd_write);
void translate_stat(struct stat* native, struct newlib_stat* newlib);
int translate_flags(int native_flags);
int translate_mode(int native_mode);
int translate_whence(int native_whence);
void translate_errno(int native, int newlib);
void set_syscall_req_payload_len(syscall_req_t* req);
void read_syscall_req(int fd, syscall_req_t* req);
void read_syscall_req_header(int fd, syscall_req_t* req);
void read_syscall_req_payload(int fd, syscall_req_t* req);
void write_syscall_rsp(int fd, syscall_rsp_t* rsp);
void write_syscall_rsp_header(int fd, syscall_rsp_t* rsp);
void write_syscall_rsp_payload(int fd, syscall_rsp_t* rsp);
int read_syscall_server(int fd, char* buf, int len);
int write_syscall_server(int fd, char* buf, int len, int bytes_to_follow); 
void error(int fd, const char* s);
char* sandbox_file_name(char* name, uint32_t len);

void handle_syscall(syscall_req_t* req, syscall_rsp_t* rsp);
void handle_open(syscall_req_t* req, syscall_rsp_t* rsp);
void handle_close(syscall_req_t* req, syscall_rsp_t* rsp);
void handle_read(syscall_req_t* req, syscall_rsp_t* rsp);
void handle_write(syscall_req_t* req, syscall_rsp_t* rsp);
void handle_link(syscall_req_t* req, syscall_rsp_t* rsp);
void handle_unlink(syscall_req_t* req, syscall_rsp_t* rsp);
void handle_lseek(syscall_req_t* req, syscall_rsp_t* rsp);
void handle_fstat(syscall_req_t* req, syscall_rsp_t* rsp);
void handle_isatty(syscall_req_t* req, syscall_rsp_t* rsp);
void handle_stat(syscall_req_t* req, syscall_rsp_t* rsp);

#endif //SERIAL_SERVER_H
