#define _LARGEFILE64_SOURCE /* needed to use lseek64 */

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

int main(int argc, char *argv[]) 
{ 
	int ret;
	int flag;
	if (argc < 4){
		fprintf(stderr, "usage: %s new old flag\n", argv[0]);
		exit(1);
	}
	flag = strtol(argv[3], 0, 0);
	argv[1] = "#p";
	argv[2] = "#r/boot";
	printf("access %s is %d\n", argv[1], access(argv[1], X_OK|R_OK));

	printf("access %s is %d\n", argv[2], access(argv[2], X_OK|R_OK));
	printf("%s %d %s %d %d\n", argv[1], strlen(argv[1]), argv[2], 
		strlen(argv[2]), flag);
	ret = syscall(145, argv[1], strlen(argv[1]), argv[2], 
		strlen(argv[2]), 0);

	printf("%d\n", ret);
}
