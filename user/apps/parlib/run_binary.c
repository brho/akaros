#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <parlib.h>

extern char * readline(const char *prompt);

#define MALLOC_SIZE     1048576
#define READ_SIZE       1024

static void fd_error() {
	fprintf(stderr, "Error: Unable to run remote binary (fd error): %s\n", strerror(errno));
}

static void malloc_error() {
	fprintf(stderr, "Error: Unable to run remote binary: No more memory avaialable!\n");
}

static void read_error(void* buf, int fd) {
	free(buf);
	close(fd);
	fprintf(stderr, "Error: Unable to run remote binary (read error): %s\n", strerror(errno));
}

static void realloc_error(void* buf, int fd) {
	free(buf);
	close(fd);
	fprintf(stderr, "Error: Unable to run remote binary: No more memory available!\n");
}

int run_binary_filename(const char* file_name)
{	
	int fd = open(file_name, O_RDONLY, 0);
	if(fd < 0) return fd;
	
	int total_bytes_read = 0;
	int bytes_read = 0;
	int bufsz = 0;
	void* binary_buf = NULL;
	
	while(1) {
		if(total_bytes_read+READ_SIZE > bufsz)
		{
			void* temp_buf = realloc(binary_buf,bufsz+MALLOC_SIZE);
			if(temp_buf == NULL) { realloc_error(binary_buf, fd); return 0; }
			binary_buf = temp_buf;
			bufsz += MALLOC_SIZE;
		}

		bytes_read = read(fd, binary_buf+total_bytes_read, READ_SIZE);
		total_bytes_read += bytes_read;
		if(bytes_read < 0) { read_error(binary_buf, fd); return 0; }
		if(bytes_read == 0) break;
	}
	printf("Loading Binary: %s, ROMSIZE: %d\n",file_name,total_bytes_read);
	ssize_t error = sys_run_binary(binary_buf, NULL, total_bytes_read, 0);
	if(error < 0) {
		fprintf(stderr, "Error: Unable to run remote binary\n");
	}
	free(binary_buf);
	close(fd);
	syscall(SYS_yield,0,0,0,0,0);
	return 0;
}

void run_binary()
{
	char* readline_result = readline("\nEnter name of binary to execute: ");
	if (readline_result == NULL) {
		printf("Error reading from console.\n");
		return;
	}
	if(run_binary_filename(readline_result) < 0)
		fd_error();
}

