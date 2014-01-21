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
	int flag = 0;
	char *src_path, *onto_path;
	/* crap arg handling for now. */
	argc--,argv++;
	if (argc > 2){
		switch(argv[0][1]){
			case 'b': flag = 1;
			break;
			case 'a': flag = 2;
			break;
			case 'c': flag = 4;
			break;
			default: 
				printf("-a or -b and/or -c for now\n");
				exit(0);
		}
		argc--, argv++;
	}

	if (argc < 2) {
		fprintf(stderr, "usage: bind [-a|-b] src_path onto_path\n");
		exit(1);
	}
	src_path = argv[0];
	onto_path = argv[1];
	printf("bind %s -> %s flag %d\n", src_path, onto_path, flag);
	ret = syscall(SYS_nbind, src_path, strlen(src_path), onto_path,
	              strlen(onto_path), flag);
	return ret;
}
