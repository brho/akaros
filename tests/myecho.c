
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
#include <ros/syscall.h>

int
main(int argc, char *argv[])
{
	argc--,argv++;
	int fd = open(argv[0], O_RDWR);
	if (fd < 0){
		perror(argv[0]);
		exit(1);
	}
	for(argc--,argv++; argc > 0; argc--, argv++){
		write(fd, argv[0], strlen(argv[0]));
	}
	close(fd);
	return 0;
}
