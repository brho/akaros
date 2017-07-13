/* Copyright (c) 2017 Google Inc.
 * See LICENSE for details.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <err.h>

/* initrd loads the initrd and returns its place in the world. It has
 * to avoid the already loaded kernel. */
ssize_t setup_initrd(char *filename, void *membase, size_t memsize)
{
	int fd;
	struct stat buf;
	void *where = membase;
	int amt;
	int tot = 0;

	if (!filename)
		return 0;

	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "Can't open %s: %r\n", filename);
		return 0;
	}

	if (fstat(fd, &buf) < 0) {
		fprintf(stderr, "Can't stat %s: %r\n", filename);
		close(fd);
		return 0;
	}

	if (buf.st_size > memsize) {
		fprintf(stderr,
		        "file is %d bytes, but we only have %d bytes to place it\n",
		        buf.st_size, memsize);
		close(fd);
		return 0;
	}

	while (tot < buf.st_size) {
		amt = read(fd, where, buf.st_size - tot);
		if (amt < 0) {
			tot = 0;
			break;
		}
		where += amt;
		tot += amt;
	}

	close(fd);
	return tot;
}
