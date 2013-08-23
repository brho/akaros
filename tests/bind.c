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
	if (argc < 2){
		fprintf(stderr, "usage: %s new old\n", argv[0]);
		exit(1);
	}

	ret = syscall(145, argv[1], strlen(argv[1]), argv[2], 
		strlen(argv[2]), 0);

	printf("%d\n", ret);
}
