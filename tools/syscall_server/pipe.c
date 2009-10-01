#include <fcntl.h>
#include <stdio.h>

// These definitions seem backwards, but they are not.
//  They are assigned from the persepctive of how qemu sees them
#define SYSCALL_SERVER_PIPE_IN  ".syscall_server_pipe.out"
#define SYSCALL_SERVER_PIPE_OUT ".syscall_server_pipe.in"

int init_syscall_server(int* fd_read, int* fd_write) {

	printf("Waiting for other end of pipe to connect...\n");
	int write = open(SYSCALL_SERVER_PIPE_OUT, O_WRONLY);
	if(write < 0)
		return write;

	int read = open(SYSCALL_SERVER_PIPE_IN, O_RDONLY);
	if(read < 0) {
		close(write);
		return read;
	}

    *fd_read = read;
	*fd_write = write;
    return read+write;
}

int read_syscall_server(int fd, char* buf, int len) {
	return read(fd, buf, len);
}

int write_syscall_server(int fd, char* buf, int len, int bytes_to_follow) {
	return write(fd, buf, len);
}


