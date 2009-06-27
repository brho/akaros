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

void run_binary()
{	
	char * readline_result = readline("\nEnter name of binary to execute: ");
	if (readline_result == NULL) {
		printf("Error reading from console.\n");
		return;
	}

	char * file_name = malloc(strlen(readline_result) + 8);
	sprintf(file_name, "./test/%s", readline_result);
	int fd = open(file_name, O_RDONLY, 0);
	
	int iters = 1;
	binary_buf = malloc(READ_SIZE);
	
	int total_bytes_read = 0;
	int bytes_read = read(fd, binary_buf, READ_SIZE);
	while(bytes_read > 0) {
		total_bytes_read += bytes_read;	
		binary_buf = realloc(binary_buf, READ_SIZE*(++iters));
		bytes_read = read(fd, binary_buf+total_bytes_read, READ_SIZE);
	}
	printf("Loading Binary: %s, ROMSIZE: %d\n", readline_result, total_bytes_read);
	sys_run_binary(binary_buf, NULL, total_bytes_read);
	free(binary_buf);
	close(fd);
}

