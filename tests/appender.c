#include <stdio.h> 
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <arch/arch.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>

#define WRITE_AMOUNT 4096
int main(int argc, char *argv[]) 
{ 
	int retval;
	char wbuf[WRITE_AMOUNT];
	if (argc < 2) {
		printf("Appends some shit to the end of a text file\n");
		printf("Usage: appender FILENAME\n");
		return -1;
	}

	int fd = open(argv[1], O_RDWR);
	if (!fd) {
		printf("Unable to open %s\n", argv[1]);
		return -1;
	}

	for (int i = 0; i < WRITE_AMOUNT; i += 4) {
		wbuf[i + 0] = 'X';
		wbuf[i + 1] = 'M';
		wbuf[i + 2] = 'E';
		wbuf[i + 3] = ' ';
	}
	
	lseek(fd, 0, SEEK_END);
	retval = write(fd, wbuf, WRITE_AMOUNT);
	printf("Tried to write %d bytes, got retval: %d\n", WRITE_AMOUNT, retval);
	return 0;
}
