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
#include <ros/syscall.h>

/* The naming for the args in bind is messy historically.  We do:
 * 		bind src_path onto_path
 * plan9 says bind NEW OLD, where new is *src*, and old is *onto*.
 * Linux says mount --bind OLD NEW, where OLD is *src* and NEW is *onto*. */
int main(int argc, char *argv[]) 
{ 
	int ret;
	int flag;
	if (argc < 4) {
		fprintf(stderr, "usage: %s src_path onto_path flag\n", argv[0]);
		exit(1);
	}
	flag = strtol(argv[3], 0, 0);
	/* til we support access in 9ns */
	//printf("access %s is %d\n", argv[1], access(argv[1], X_OK|R_OK));
	//printf("access %s is %d\n", argv[2], access(argv[2], X_OK|R_OK));
	printf("%s %d %s %d %d\n", argv[1], strlen(argv[1]), argv[2], 
	       strlen(argv[2]), flag);
	ret = syscall(SYS_nbind, argv[1], strlen(argv[1]), argv[2],
	              strlen(argv[2]), 0);

	printf("%d\n", ret);
}
