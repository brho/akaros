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

static void usage_exit(void)
{
	fprintf(stderr, "usage: bind [-a|-b] [-c] [-v] src_path onto_path\n");
	exit(1);
}

/* The naming for the args in bind is messy historically.  We do:
 * 	bind src_path onto_path
 * plan9 says bind NEW OLD, where new is *src*, and old is *onto*.
 * Linux says mount --bind OLD NEW, where OLD is *src* and NEW is *onto*.
 *
 * Maybe we should go with WHAT WHERE... */
int main(int argc, char *argv[])
{
	int ret;
	int flag = 0;
	char *src_path, *onto_path;
	bool verbose = false;

	/* crap arg handling for now. */
	argc--, argv++;
	while (argc > 2) {
		switch (argv[0][1]) {
		case 'b':
			flag |= 1;
			break;
		case 'a':
			flag |= 2;
			break;
		case 'c':
			flag |= 4;
			break;
		case 'v':
			verbose = true;
			break;
		default:
			usage_exit();
		}
		/* extremely disgusting */
		if (argv[0][2]) {
			fprintf(stderr, "sorry, only one argument per -\n");
			usage_exit();
		}
		argc--, argv++;
	}

	if (argc < 2)
		usage_exit();
	src_path = argv[0];
	onto_path = argv[1];
	if (verbose)
		printf("bind %s (onto) -> %s (src) flag %d\n", onto_path,
		       src_path, flag);
	ret = syscall(SYS_nbind, src_path, strlen(src_path), onto_path,
	              strlen(onto_path), flag);
	if (ret < 0)
		perror("Bind failed");
	return ret;
}
