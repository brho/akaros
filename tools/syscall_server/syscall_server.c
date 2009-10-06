#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <malloc.h>
#include <termios.h>
#include <strings.h>
#include "syscall_server.h"

#define debug(...) printf(__VA_ARGS__)  

int main()
{
	run_server();
	return 0;
}

// Poll for incoming messages and send responses. 
void run_server() 
{
	// Struct for reading the syscall over the channel
	syscall_req_t syscall_req;
	syscall_rsp_t syscall_rsp;
	
	int fd_read, fd_write;
	int ret = init_syscall_server(&fd_read, &fd_write);
	if(ret < 0)
		error(ret, "Could not open file desciptors for communication\n");

	printf("Server started....");
	// Continuously read in data from the channel socket
	while(1) {
		syscall_req.payload_len = 0;
		syscall_req.payload = NULL;
		syscall_rsp.payload_len = 0;
		syscall_rsp.payload = NULL;
	
		debug("\nWaiting for syscall...\n");
		read_syscall_req(fd_read, &syscall_req);	

		debug("Processing syscall: %d\n", syscall_req.header.id);
		handle_syscall(&syscall_req, &syscall_rsp);

		debug("Writing response: %d\n", syscall_req.header.id);
		write_syscall_rsp(fd_write, &syscall_rsp);

		if(syscall_req.payload != NULL) 
			free(syscall_req.payload);
		if(syscall_rsp.payload != NULL)
			free(syscall_rsp.payload);
	}
}

void read_syscall_req(int fd, syscall_req_t* req) 
{
	read_syscall_req_header(fd, req);
	set_syscall_req_payload_len(req);
	read_syscall_req_payload(fd, req);
}

void set_syscall_req_payload_len(syscall_req_t* req)
{
	switch(req->header.id) {
		case OPEN_ID:
			req->payload_len = req->header.subheader.open.len;
			break;
		case WRITE_ID:
			req->payload_len = req->header.subheader.write.len;
			break;
		case LINK_ID:
			req->payload_len = req->header.subheader.link.old_len
			                   + req->header.subheader.link.new_len;
			break;
		case UNLINK_ID:
			req->payload_len = req->header.subheader.unlink.len;
			break;
		case STAT_ID:
			req->payload_len = req->header.subheader.stat.len;
			break;
	}
}

void read_syscall_req_header(int fd, syscall_req_t* req) 
{
	// Try to read the syscall id from the socket.
	// If no data available, spin until there is
	int bytes_read = 0;
	bytes_read = read_from_channel(fd, &req->header, sizeof(req->header.id), 0);

   	// If no data, or the ID we got is bad, terminate process.
	uint32_t id = req->header.id;
   	if ((bytes_read < 0) || (id < 0) || (id > NUM_SYSCALLS)) {
		perror("Problems reading the id from the channel...");
	}

   	// Otherwise, start grabbing the rest of the data
   	bytes_read = read_from_channel(fd, &req->header.subheader, 
                                  sizeof(req->header.subheader) , 0);
   	if(bytes_read < 0)
		error(fd, "Problems reading header from the channel...");
}

void read_syscall_req_payload(int fd, syscall_req_t* req) {
	if (req->payload_len == 0)
		return;
   		
	req->payload = malloc(req->payload_len);
	if (req->payload == NULL) 
		error(fd, "No free memory!");

	int bytes_read = read_from_channel(fd, req->payload, req->payload_len, 0);
	if (bytes_read < 0)
		error(fd, "Problems reading payload from channel");
}

// Read len bytes from the given socket to the buffer.
// If peek is 0, will wait indefinitely until that much data is read.
// If peek is 1, if no data is available, will return immediately.
int read_from_channel(int fd, void* buf, int len, int peek) 
{
	int total_read = 0;
	int just_read = read_syscall_server(fd, buf, len);

	if (just_read < 0) return just_read;
	if (just_read == 0 && peek) return just_read;

	total_read += just_read;

	while (total_read != len) {
		just_read = read_syscall_server(fd, buf + total_read, len - total_read);
		if (just_read < 0) return just_read;
		total_read += just_read;
	}
	return total_read;
}

// Send CONNECTION_TERMINATED over the FD (if possible)
void error(int fd, const char* s)
{
	fprintf(stderr, "Error: FD: %i\n",fd);
	perror(s);
    fprintf(stderr, "Sending CONNECTION_TERMINATED.... \n");
	close(fd);
	exit(-1);
}

void handle_syscall(syscall_req_t* req, syscall_rsp_t* rsp)
{
	switch (req->header.id) {
		case OPEN_ID:
			handle_open(req, rsp);
			break;
		case CLOSE_ID:
			handle_close(req, rsp);
			break;
		case READ_ID:
			handle_read(req, rsp);
			break;
		case WRITE_ID:
			handle_write(req, rsp);
			break;
		case LINK_ID:
			handle_link(req, rsp);
			break;
		case UNLINK_ID:
			handle_unlink(req, rsp);
			break;
		case LSEEK_ID:
			handle_lseek(req, rsp);
			break;
		case FSTAT_ID:
			handle_fstat(req, rsp);
			break;
		case ISATTY_ID:
			handle_isatty(req, rsp);
			break;
		case STAT_ID:
			handle_stat(req, rsp);
			break;
		default:
			error(-1, "Illegal syscall, should never be here...");
	}
	
	rsp->header.return_errno = errno;
}

void write_syscall_rsp(int fd, syscall_rsp_t* rsp) 
{
	write_syscall_rsp_header(fd, rsp);
	write_syscall_rsp_payload(fd, rsp);
}

void write_syscall_rsp_header(int fd, syscall_rsp_t* rsp) 
{
   	int written = write_syscall_server(fd, (char*)&rsp->header, 
	                   sizeof(syscall_rsp_header_t), rsp->payload_len);
	if (written < 0)
		error(fd, "Problems writing the syscall response header...");	
}

void write_syscall_rsp_payload(int fd, syscall_rsp_t* rsp) 
{
	if(rsp->payload_len == 0)
		return;

   	int written = write_syscall_server(fd, rsp->payload, rsp->payload_len, 0);
	if (written < 0)
		error(fd, "Problems writing the syscall response payload...");	
	if (written < rsp->payload_len)
		error(fd, "Problems writing all bytes in the response payload...");	
}

char* sandbox_file_name(char* name, uint32_t len) {
	char* new_name = malloc(len + sizeof(SANDBOX_DIR) - 1);
	if (new_name == NULL) 
		perror("No free memory!");
	sprintf(new_name, "%s%s", SANDBOX_DIR, name);
	printf("%s\n", new_name);
	return new_name;
}

void handle_open(syscall_req_t* req, syscall_rsp_t* rsp)
{
	char* name = sandbox_file_name(req->payload, req->payload_len);
	open_subheader_t* o = &req->header.subheader.open;	
	int native_flags = translate_flags(o->flags);
	int native_mode = translate_mode(o->mode);
	rsp->header.return_val = open(name, native_flags, native_mode);
	free(name);
}

void handle_close(syscall_req_t* req, syscall_rsp_t* rsp)
{
	close_subheader_t* c = &req->header.subheader.close;	
	rsp->header.return_val = close(c->fd);
}

void handle_read(syscall_req_t* req, syscall_rsp_t* rsp)
{
	read_subheader_t* r = &req->header.subheader.read;	
	rsp->payload = malloc(r->len);
	if (rsp->payload == NULL) 
		perror("No free memory!");
	rsp->header.return_val = read(r->fd, rsp->payload, r->len);
	if(rsp->header.return_val >= 0)
		rsp->payload_len = rsp->header.return_val;
}

void handle_write(syscall_req_t* req, syscall_rsp_t* rsp)
{
	write_subheader_t* w = &req->header.subheader.write;	
	rsp->header.return_val = write(w->fd, req->payload, w->len);
}

void handle_link(syscall_req_t* req, syscall_rsp_t* rsp)
{
	link_subheader_t* l = &req->header.subheader.link;	
	char* old_name = sandbox_file_name(req->payload, l->old_len);
	char* new_name = sandbox_file_name(req->payload + l->old_len, l->new_len);
	rsp->header.return_val = link(old_name, new_name); 
	free(old_name);
	free(new_name);
}

void handle_unlink(syscall_req_t* req, syscall_rsp_t* rsp)
{
	char* name = sandbox_file_name(req->payload, req->payload_len);
	rsp->header.return_val = unlink(name); 
	free(name);
}

void handle_lseek(syscall_req_t* req, syscall_rsp_t* rsp)
{
	lseek_subheader_t* l = &req->header.subheader.lseek;	
	int native_whence = translate_whence(l->dir); 
	rsp->header.return_val = lseek(l->fd, l->ptr, native_whence);
}

void handle_fstat(syscall_req_t* req, syscall_rsp_t* rsp)
{
	struct stat native_struct;
	fstat_subheader_t* f = &req->header.subheader.fstat;	
	rsp->payload = malloc(sizeof(newlib_stat_t));
	if (rsp->payload == NULL) 
		perror("No free memory!");
	rsp->header.return_val = fstat(f->fd, &native_struct); 
	if(rsp->header.return_val >= 0)
		rsp->payload_len = sizeof(newlib_stat_t);
	
	translate_stat(&native_struct, (newlib_stat_t*)(rsp->payload));
}

void handle_isatty(syscall_req_t* req, syscall_rsp_t* rsp)
{
	isatty_subheader_t* i = &req->header.subheader.isatty;	
	rsp->header.return_val = isatty(i->fd); 
}

void handle_stat(syscall_req_t* req, syscall_rsp_t* rsp)
{
	struct stat native_struct;
	rsp->payload = malloc(sizeof(newlib_stat_t));
	if (rsp->payload == NULL) 
		perror("No free memory!");
	rsp->header.return_val = stat(req->payload, &native_struct); 
	if(rsp->header.return_val >= 0)
		rsp->payload_len = sizeof(newlib_stat_t);

	translate_stat(&native_struct, (newlib_stat_t*)(rsp->payload));
}

