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

#define READ_SIZE       1024
uint8_t* binary_buf;

static void fd_error() {
	fprintf(stderr, "Error: Unable to run remote binary (fd error): %s\n", strerror(errno));
}

static void malloc_error() {
	fprintf(stderr, "Error: Unable to run remote binary: No more memory avaialable!\n");
}

static void read_error(void* buf, int fd) {
	free(binary_buf);
	close(fd);
	fprintf(stderr, "Error: Unable to run remote binary (read error): %s\n", strerror(errno));
}

static void realloc_error(void* buf, int fd) {
	free(binary_buf);
	close(fd);
	fprintf(stderr, "Error: Unable to run remote binary: No more memory available!\n");
}

void run_binary()
{	
	char * readline_result = readline("\nEnter name of binary to execute: ");
	if (readline_result == NULL) {
		printf("Error reading from console.\n");
		return;
	}

	char * file_name = malloc(strlen(readline_result) + 8);
	sprintf(file_name, "./apps/%s", readline_result);
	int fd = open(file_name, O_RDONLY, 0);
	if(fd < 0) { fd_error(); return; };
	
	int iters = 1;
	binary_buf = malloc(READ_SIZE);
	if(binary_buf == NULL) { malloc_error(); return; }
	
	int total_bytes_read = 0;
	int bytes_read = read(fd, binary_buf, READ_SIZE);
	if(bytes_read < 0) { read_error(binary_buf, fd); return; }
	
	while(bytes_read > 0) {
		total_bytes_read += bytes_read;	
		void* temp_buf = realloc(binary_buf, READ_SIZE*(++iters));
		if(temp_buf == NULL) { realloc_error(binary_buf, fd); return; }	
		binary_buf = temp_buf;
		bytes_read = read(fd, binary_buf+total_bytes_read, READ_SIZE);
		if(bytes_read < 0) { read_error(binary_buf, fd); return; }
	}
	printf("Loading Binary: %s, ROMSIZE: %d\n", readline_result, total_bytes_read);
	ssize_t error = sys_run_binary(binary_buf, NULL, total_bytes_read);
	if(error < 0) {
		fprintf(stderr, "Error: Unable to run remote binary\n");
	}
	free(binary_buf);
	close(fd);

}

