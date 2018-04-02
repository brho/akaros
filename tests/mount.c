#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <parlib/arch/arch.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <ros/syscall.h>

int main(int argc, char *argv[])
{
	int fd;
	int flag = 0;
	int ret;

	/* crap arg handling for now. */
	argc--, argv++;
	while (argc > 2) {
		switch(argv[0][1]){
		case 'b':
			flag |= 1;
			break;
		case 'a':
			flag |= 2;
			break;
		case 'c':
			flag |= 4;
			break;
		case 'C':
			flag |= 0x10;
			break;
		default:
			printf("-a or -b and/or -c and/or -C for now\n");
			exit(-1);
		}
		argc--, argv++;
	}

	if (argc < 2) {
		fprintf(stderr, "usage: mount [-a|-b|-c|-C] channel onto_path\n");
		exit(-1);
	}
	fd = open(argv[0], O_RDWR);
	if (fd < 0) {
		perror("Unable to open chan for mounting");
		exit(-1);
	}
	ret = syscall(SYS_nmount, fd, argv[1], strlen(argv[1]), flag);
	if (ret < 0) {
		perror("sys_mount:");
		return -1;
	}
	return 0;
}
