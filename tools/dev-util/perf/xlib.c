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

FILE *xfdopen(int fd, const char *mode)
{
	FILE *file = fdopen(fd, mode);

	if (!file) {
		fprintf(stderr,
			"Unable to reopen fd '%d' for mode '%s': %s\n", fd,
			mode, strerror(errno));
		exit(1);
	}

	return file;
}

off_t xfsize(FILE *file)
{
	struct stat stat_buf;
	int fd = fileno(file);

	if (fd < 0) {
		perror("xfsize fileno");
		exit(1);
	}
	if (fstat(fd, &stat_buf)) {
		perror("xfsize fstat");
		exit(1);
	}
	return stat_buf.st_size;
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

		fprintf(stderr,
			"Unable to seek at offset %ld from %s (fpos=%ld): %s\n",
			offset, whence == SEEK_SET ? "beginning of file" :
				(whence == SEEK_END ? "end of file" :
				 "current position"),
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

uint8_t nibble_to_num(char c)
{
	switch (c) {
	case '0':
		return 0;
	case '1':
		return 1;
	case '2':
		return 2;
	case '3':
		return 3;
	case '4':
		return 4;
	case '5':
		return 5;
	case '6':
		return 6;
	case '7':
		return 7;
	case '8':
		return 8;
	case '9':
		return 9;
	case 'a':
	case 'A':
		return 0xa;
	case 'b':
	case 'B':
		return 0xb;
	case 'c':
	case 'C':
		return 0xc;
	case 'd':
	case 'D':
		return 0xd;
	case 'e':
	case 'E':
		return 0xe;
	case 'f':
	case 'F':
		return 0xf;
	};
	return -1;
}
