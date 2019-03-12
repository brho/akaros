#define _LARGEFILE64_SOURCE /* needed to use lseek64 */

#include <parlib/stdio.h>
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
#include <parlib/parlib.h>

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
	int talk[2];
	char hi[3] = {0};
	char *child_argv[3];

	/* detect the child by the number of args. */
	if (argc > 1) {
		read(0, hi, 3);
		assert(!strcmp(hi, "HI"));
		/* pass something else back */
		hi[0] = 'Y';
		hi[1] = 'O';
		hi[2] = '\0';
		write(1, hi, 3);
		exit(0);
	}

	if (pipe(talk) < 0) {
		perror("pipe");
		exit(1);
	}
	printd("pipe [%d, %d]\n", talk[0], talk[1]);

	/* parent will read and write on talk[0], and the child does the same
	 * for talk[1].  internally, writing to talk 0 gets read on talk 1.  the
	 * child gets talk1 mapped for both stdin (fd 0) and stdout (fd 1). */
	childfdmap[0].parentfd = talk[1];
	childfdmap[0].childfd = 0;
	childfdmap[1].parentfd = talk[1];
	childfdmap[1].childfd = 1;

	sprintf(filename, "/bin/%s", argv[0]);
	child_argv[0] = filename;
	child_argv[1] = "1"; /* dummy arg, signal so we know they're the child*/
	child_argv[2] = 0;

	kid = sys_proc_create(filename, strlen(filename), child_argv, NULL, 0);
	if (kid < 0) {
		perror("create failed");
		exit(1);
	}

	ret = syscall(SYS_dup_fds_to, kid, childfdmap, 2);
	if (ret != 2) {
		perror("childfdmap faled");
		exit(2);
	}

	/* close the pipe endpoint that we duped in the child.  it doesn't
	 * matter for this test, but after the child exits, the pipe will still
	 * be open unless we close our side of it. */
	close(talk[1]);

	sys_proc_run(kid);

	if (write(talk[0], "HI", 3) < 3) {
		perror("write HI");
		exit(3);
	}

	if (read(talk[0], hi, 3) < 3) {
		perror("read YO");
		exit(4);
	}
	assert(!strcmp(hi, "YO"));

	printf("Passed\n");

	return 0;
}
