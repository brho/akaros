/* Copyright (c) 2016 Google Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Usage: wrmsr MSR VAL
 *
 * This will write VAL to *all cores* MSR.
 *
 * e.g. wrmsr 0x199 0x100002600 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	uint32_t msr;
	uint64_t val;
	int fd;
	ssize_t ret;

	if (argc < 3) {
		printf("Usage: %s MSR VAL\n", argv[0]);
		exit(-1);
	}
	msr = strtoul(argv[1], 0, 0);
	val = strtoul(argv[2], 0, 0);

	fd = open("#arch/msr", O_RDWR);
	if (fd < 0) {
		perror("open");
		exit(-1);
	}
	ret = pwrite(fd, &val, sizeof(val), msr);
	if (ret < 0) {
		perror("pwrite");
		exit(-1);
	}
	return 0;
}
