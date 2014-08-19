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
#include <parlib.h>

/* Test the childfdmap system call.
 * Create a pipe, start the spawn, dup the pipes over fd 0 and 1, write
 * to it, see that you get the same back.
 */
char filename[512];
int main(int argc, char *argv[]) 
{ 
	struct childfdmap childfdmap[3];
	int ret;
	int flag = 0;
	int kid;
	int in[2], out[2];
	char hi[3];
	if (pipe(in) < 0) {
		perror("pipe");
		exit(1);
	}
	if (pipe(out) < 0) {
		perror("2nd pipe");
		exit(1);
	}
	printf("pipe(in) [%d, %d]; pipe(out)[%d, %d]\n", in[0], in[1], out[0], out[1]);
	childfdmap[0].parentfd = in[0];
	childfdmap[0].childfd = in[1];
	childfdmap[1].parentfd = out[0];
	childfdmap[1].childfd = out[1];

	sprintf(filename, "/bin/%s", argv[0]);
	kid = sys_proc_create(filename, strlen(filename), NULL, NULL, 0);
	if (kid < 0) {
		perror("create failed");
		exit(1);
	}

	ret = syscall(65536, kid, childfdmap, 2);
	if (ret < 0) {
		perror("childfdmap faled");
		exit(2);
	}


	sys_proc_run(kid);
	if (write(childfdmap[0].parentfd, "HI", 2) < 2) {
		perror("write HI");
		exit(3);
	}

	if (read(childfdmap[1].parentfd, hi, 2) < 2) {
		perror("read HI");
		exit(4);
	}

	return 0;
}
