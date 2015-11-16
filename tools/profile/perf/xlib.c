/* Copyright (c) 2015 Google Inc
 * Davide Libenzi <dlibenzi@google.com>
 * See LICENSE for details.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include "xlib.h"

int xopen(const char *path, int flags, mode_t mode)
{
	int fd = open(path, flags, mode);

	if (fd < 0) {
		perror(path);
		exit(1);
	}

	return fd;
}

void xwrite(int fd, const void *data, size_t size)
{
	ssize_t wcount = write(fd, data, size);

	if (size != (size_t) wcount) {
		perror("Writing file");
		exit(1);
	}
}

void xread(int fd, void *data, size_t size)
{
	ssize_t rcount = read(fd, data, size);

	if (size != (size_t) rcount) {
		perror("Reading file");
		exit(1);
	}
}

void xpwrite(int fd, const void *data, size_t size, off_t off)
{
	ssize_t wcount = pwrite(fd, data, size, off);

	if (size != (size_t) wcount) {
		perror("Writing file");
		exit(1);
	}
}

void xpread(int fd, void *data, size_t size, off_t off)
{
	ssize_t rcount = pread(fd, data, size, off);

	if (size != (size_t) rcount) {
		perror("Reading file");
		exit(1);
	}
}

void *xmalloc(size_t size)
{
	void *data = malloc(size);

	if (!data) {
		perror("Allocating memory block");
		exit(1);
	}

	return data;
}

void *xzmalloc(size_t size)
{
	void *data = xmalloc(size);

	memset(data, 0, size);

	return data;
}

char *xstrdup(const char *str)
{
	char *dstr = strdup(str);

	if (dstr == NULL) {
		perror("Duplicating a string");
		exit(1);
	}

	return dstr;
}
