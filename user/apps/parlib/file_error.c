#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>

#define IN_BUF_SIZE 1024

void file_error()
{
		
	char buf[IN_BUF_SIZE];

	int dne_fd = 99;
	int result;

	printf("Starting error testing....\n\n");

	errno = 0;
	int bad_fd = open("DNE", O_RDWR, 0);
	printf("Opened:       DNE\n");
        printf("FD:           %d\n", bad_fd);
        printf("ERRNO:        %s\n", strerror(errno));


        errno = 0;
        printf("\n");
        result = close(dne_fd);
        printf("CLOSED FD:    %d\n", dne_fd);
        printf("RESULT:       %d\n", result);
        printf("ERRNO:        %s\n", strerror(errno));

	errno = 0;
	printf("\n");
	result = read(bad_fd, buf, IN_BUF_SIZE - 1);
	printf("Read:         %d bytes\n", result);
	printf("ERRNO:        %s\n", strerror(errno));

	errno = 0;
	printf("\n");
        result = write(bad_fd, buf, IN_BUF_SIZE - 1);
        printf("Wrote:        %d bytes\n", result);
        printf("ERRNO:        %s\n", strerror(errno));

	errno = 0;
	printf("\n");
	result = unlink("DNE");
        printf("UNLINKED:     DNE\n");
	printf("RESULT:       %d\n", result);
        printf("ERRNO:        %s\n", strerror(errno));
	
        errno = 0;
        printf("\n");
        result = link("DNE", "DEST");
        printf("LINKED:       DEST to DNE\n");
        printf("RESULT:       %d\n", result);
        printf("ERRNO:        %s\n", strerror(errno));

	errno = 0;
        printf("\n");
        result = isatty(dne_fd);
        printf("ISATTY on FD: %d\n", dne_fd);
        printf("RESULT:       %d\n", result);
        printf("ERRNO:        %s\n", strerror(errno));

	errno = 0;
        printf("\n");
        result = lseek(dne_fd, 0, 0);
        printf("LSEEKED FD:   %d\n", dne_fd);
        printf("RESULT:       %d\n", result);
        printf("ERRNO:        %s\n", strerror(errno));

	struct stat st;

	errno = 0;
        printf("\n");
        result = stat("DNE", &st);
        printf("STAT:         DNE\n");
        printf("RESULT:       %d\n", result);
        printf("ERRNO:        %s\n", strerror(errno));

        errno = 0;
        printf("\n");
        result = fstat(dne_fd, &st);
        printf("FSTAT on FD:  %d\n", dne_fd);
        printf("RESULT:       %d\n", result);
        printf("ERRNO:        %s\n", strerror(errno));

	printf("\nTests Complete.\n\n");
}
