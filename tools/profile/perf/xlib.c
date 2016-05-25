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

FILE *xfopen(const char *path, const char *mode)
{
	FILE *file = fopen(path, mode);

	if (!file) {
		fprintf(stderr, "Unable to open file '%s' for mode '%s': %s\n",
				path, mode, strerror(errno));
		exit(1);
	}

	return file;
}

off_t xfsize(FILE *file)
{
	off_t pos = ftello(file), size;

	xfseek(file, 0, SEEK_END);
	size = ftello(file);
	xfseek(file, pos, SEEK_SET);

	return size;
}

void xfwrite(const void *data, size_t size, FILE *file)
{
	if (fwrite(data, 1, size, file) != size) {
		fprintf(stderr, "Unable to write %lu bytes: %s\n", size,
				strerror(errno));
		exit(1);
	}
}

void xfseek(FILE *file, off_t offset, int whence)
{
	if (fseeko(file, offset, whence)) {
		int error = errno;

		fprintf(stderr, "Unable to seek at offset %ld from %s (fpos=%ld): %s\n",
				offset, whence == SEEK_SET ? "beginning of file" :
				(whence == SEEK_END ? "end of file" : "current position"),
				ftell(file), strerror(error));
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

const char *vb_decode_uint64(const char *data, uint64_t *pval)
{
	unsigned int i;
	uint64_t val = 0;

	for (i = 0; (*data & 0x80) != 0; i += 7, data++)
		val |= (((uint64_t) *data) & 0x7f) << i;
	*pval = val | ((uint64_t) *data) << i;

	return data + 1;
}

int vb_fdecode_uint64(FILE *file, uint64_t *pval)
{
	unsigned int i = 0;
	uint64_t val = 0;

	for (;;) {
		int c = fgetc(file);

		if (c == EOF)
			return EOF;
		val |= (((uint64_t) c) & 0x7f) << i;
		i += 7;
		if ((c & 0x80) == 0)
			break;
	}
	*pval = val;

	return i / 7;
}
