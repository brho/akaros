/* Copyright (c) 2017 Google Inc.
 * See LICENSE for details.
 *
 * Utility functions. */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <vmm/util.h>

ssize_t cat(char *filename, void *where, size_t memsize)
{
	int fd;
	ssize_t amt, tot = 0;
	struct stat buf;

	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "Can't open %s: %r\n", filename);
		return -1;
	}

	if (fstat(fd, &buf) < 0) {
		fprintf(stderr, "Can't stat %s: %r\n", filename);
		close(fd);
		return -1;
	}

	if (buf.st_size > memsize) {
		fprintf(stderr,
		        "file is %d bytes, but we only have %d bytes to place it\n",
		        buf.st_size, memsize);
		errno = ENOMEM;
		close(fd);
		return -1;
	}

	while (tot < buf.st_size) {
		amt = read(fd, where, buf.st_size - tot);
		if (amt < 0) {
			tot = -1;
			break;
		}
		if (amt == 0)
			break;
		where += amt;
		tot += amt;
	}

	close(fd);
	return tot;
}
