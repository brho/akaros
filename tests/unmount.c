#define _LARGEFILE64_SOURCE /* needed to use lseek64 */

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

/* The naming for the args in bind is messy historically.  We do:
 * 		bind src_path onto_path
 * plan9 says bind NEW OLD, where new is *src*, and old is *onto*.
 * Linux says mount --bind OLD NEW, where OLD is *src* and NEW is *onto*.
 *
 * For unmount, we use the same names as for bind. */
int main(int argc, char *argv[]) 
{ 
	int ret;
	int flag = 0;
	char *src_path, *onto_path;

	switch (argc) {
	case 3:
		src_path = argv[1];
		onto_path = argv[2];
		break;
	case 2:
		src_path = NULL;
		onto_path = argv[1];
		break;
	default:
		fprintf(stderr, "usage: unmount [src_path] onto_path\n");
		exit(1);
	}
	ret = syscall(SYS_nunmount, src_path, src_path ? strlen(src_path) : 0,
	              onto_path, strlen(onto_path));
	if (ret < 0)
		perror("Unmount failed");
	return ret;
}
