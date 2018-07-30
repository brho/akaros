#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

int main(int argc, char **argv)
{
	int fd, ret;
	char *path = ".";

	if (argc > 2) {
		fprintf(stderr, "Usage: %s [PATH]\n", argv[0]);
		exit(-1);
	}
	if (argc == 2)
		path = argv[1];
	fd = open(path, O_READ);
	if (fd < 0) {
		perror("open");
		exit(-1);
	}
	ret = fcntl(fd, CCTL_DEBUG);
	if (ret)
		perror("fcntl CCTL_DEBUG");
	close(fd);
	return ret;
}
