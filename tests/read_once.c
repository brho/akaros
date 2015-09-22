/* Copyright (c) 2015 Google Inc
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Debugging app: read_once PATH
 *
 * opens PATH, does one read syscall, outputs resulting buffer as a string. */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>


#define handle_error(msg) \
        do { perror(msg); exit(-1); } while (0)

int main(int argc, char *argv[])
{
	char *path;
	int fd, ret;
	char buf[1024];

	if (argc != 2) {
		printf("Usage: %s PATH\n", argv[0]);
		exit(-1);
	}
	path = argv[1];

	fd = open(path, O_READ);
	if (fd < 0)
		handle_error("Can't open path");
	ret = read(fd, buf, sizeof(buf));
	if (ret < 0)
		handle_error("Can't read");
	buf[ret] = 0;
	printf("%s", buf);
	close(fd);
	return 0;
}
