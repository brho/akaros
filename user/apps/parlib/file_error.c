#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>

#define IN_BUF_SIZE 1024

extern char * readline(const char *prompt);

void file_error()
{
		
	char buf[IN_BUF_SIZE];

	printf("Starting error testing....\n\n");

	errno = 0;
	int bad_fd = open("./test/DNE", O_RDWR, 0);
	printf("Opened:       DNE\n");
        printf("FD:           %d\n", bad_fd);
        printf("ERRNO:        %s\n", strerror(errno));

	errno = 0;
	int result = read(bad_fd, buf, IN_BUF_SIZE - 1);
	printf("Read:         %d bytes\n", result);
	printf("ERRNO:        %s\n", strerror(errno));

	errno = 0;
	result = unlink("DNE");
        printf("UNLINKED:     DNE\n");
	printf("RESULT:       %d\n", result);
        printf("ERRNO:        %s\n", strerror(errno));
	
	printf("\nTests Complete.\n\n");
}
