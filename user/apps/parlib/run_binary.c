#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <parlib.h>
#include <newlib_backend.h>

int shell_exec(const char* cmdline)
{
	#define MY_MAX_ARGV 16
	char* argv[MY_MAX_ARGV+1] = {0};
	char* p0 = strdup(cmdline);
	assert(p0);
	char* p = p0;
	for(int i = 0; i < MY_MAX_ARGV; i++)
	{
		argv[i] = p;
		p = strchr(p,' ');
		if(p)
			*p++ = 0;
		else
			break;
	}

	int ret = fork();
	if(ret == 0)
	{
		char** envp = environ;
		const char* path = NULL;
		int nenv;
		for(nenv = 0; environ[nenv]; nenv++)
			if(strncmp(environ[nenv],"PATH=",5) == 0)
				path = environ[nenv]+5;
		assert(path);

		char* fn = NULL, *buf = NULL;
		if(strchr(argv[0],'/'))
		{
			if(access(argv[0],X_OK) == 0)
				fn = argv[0];
		}
		else
		{
			buf = (char*)malloc(sizeof(char)*(strlen(argv[0])+strlen(path)+2));
			while(fn == NULL)
			{
				const char* end = strchr(path,':');
				int len = end ? end-path : strlen(path);
				memcpy(buf,path,len);
				if(len && buf[len-1] != '/')
					buf[len++] = '/';
				strcpy(buf+len,argv[0]);

				if(access(buf,X_OK) == 0)
					fn = buf;
				if(end == NULL)
					break;
				path = end+1;
			}
		}

		if(fn == NULL)
		{
			printf("%s: not found\n",argv[0]);
			exit(1);
		}

		execve(fn,argv,envp);
		free(buf);
		perror("execvp");
		exit(1);
	}
	else if(ret > 0)
	{
		int status;
		if(wait(&status))
			perror("wait");
		else
			debug_in_out("%s returned %d\n",argv[0],status);
	}
	else
		perror("fork");

	free(p0);
	return 0;
}

extern char * readline(const char *prompt);

#define MALLOC_SIZE     1048576
#define READ_SIZE       1024

#if 0
int run_binary_filename(const char* cmdline, size_t colors)
{
	int ret = 0;

	const char* cmdptr = cmdline;
	char argv_buf[PROCINFO_ARGBUF_SIZE] = {0};
	intreg_t* argv = (intreg_t*)argv_buf;
	argv[0] = 0;
	int argc;
	for(argc = 0; ; argc++)
	{
		while(*cmdptr == ' ')
			cmdptr++;
		if(*cmdptr == 0)
			break;

		char* p = strchr(cmdptr,' ');
		int len = p == NULL ? 1+strlen(cmdptr) : 1+p-cmdptr;

		argv[argc+1] = argv[argc]+len;

		if(p == NULL)
		{
			argc++;
			break;
		}

		cmdptr = p;
	}
	for(int i = 0; i < argc; i++)
	{
		intreg_t offset = argv[i];
		argv[i] += (argc+1)*sizeof(char*);
		memcpy(argv_buf+argv[i],cmdline+offset,argv[i+1]-offset-1);
		argv_buf[argv[i]+argv[i+1]-offset-1] = 0;
	}
	argv[argc] = 0;

	char* filename = argv_buf+argv[0];
	int fd = open(filename, O_RDONLY, 0);
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
	//printf("Loading Binary: %s, ROMSIZE: %d\n",filename,total_bytes_read);
	ret = sys_run_binary(binary_buf, total_bytes_read, argv_buf, colors);
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
#endif

void run_binary(size_t colors)
{
	char* readline_result = readline("\nEnter name of binary to execute: ");
	if (readline_result == NULL) {
		printf("Error reading from console.\n");
		return;
	}
	shell_exec(readline_result);
	//run_binary_filename(readline_result, colors);
}
