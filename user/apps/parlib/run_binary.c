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

int run_binary_filename(const char* cmdline, size_t colors)
{
	int ret = 0;

	char argv_buf[PROCINFO_MAX_ARGV_SIZE] = {0};
	char* argv_buf_ptr = argv_buf;
	for(int argc = 0; ; argc++)
	{
		while(*cmdline == ' ')
			cmdline++;
		if(*cmdline == 0)
			break;

		char* p = strchr(cmdline,' ');
		int len = p == NULL ? strlen(cmdline) : p-cmdline;

		memcpy(argv_buf_ptr,cmdline,len);
		argv_buf_ptr[len] = 0;
		argv_buf_ptr += len+1;

		if(p == NULL)
		{
			argc++;
			break;
		}

		cmdline = p;
	}
	

	int fd = open(argv_buf, O_RDONLY, 0);
	if(fd < 0)
	{
		printf("open failed\n");
		ret = -1;
		goto open_error;
	}
	
	int total_bytes_read = 0;
	int bytes_read = 0;
	int bufsz = 0;
	void* binary_buf = NULL;
	
	while(1) {
		if(total_bytes_read+READ_SIZE > bufsz)
		{
			void* temp_buf = realloc(binary_buf,bufsz+MALLOC_SIZE);
			if(temp_buf == NULL)
			{
				printf("realloc failed\n");
				ret = -1;
				goto realloc_error;
			}

			binary_buf = temp_buf;
			bufsz += MALLOC_SIZE;
		}

		bytes_read = read(fd, binary_buf+total_bytes_read, READ_SIZE);
		total_bytes_read += bytes_read;
		if(bytes_read < 0)
		{
			printf("read error\n");
			ret = -1;
			goto read_error;
		}
		if(bytes_read == 0) break;
	}
	printf("Loading Binary: %s, ROMSIZE: %d\n",argv_buf,total_bytes_read);
	ret = sys_run_binary(binary_buf, total_bytes_read, argv_buf, PROCINFO_MAX_ARGV_SIZE, colors);
	if(ret < 0)
		fprintf(stderr, "Error: Unable to run remote binary\n");
	else
		syscall(SYS_yield,0,0,0,0,0);

read_error:
realloc_error:
	free(binary_buf);
	close(fd);
open_error:
	return ret;
}

void run_binary(size_t colors)
{
	char* readline_result = readline("\nEnter name of binary to execute: ");
	if (readline_result == NULL) {
		printf("Error reading from console.\n");
		return;
	}
	run_binary_filename(readline_result, colors);
}

