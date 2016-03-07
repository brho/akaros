/* Copyright (c) 2016 Google Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Usage: rdmsr MSR
 *
 * This will read MSR on all cores.
 *
 * e.g. rdmsr 0x199 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>
#include <unistd.h>
#include <parlib/sysinfo.h>

int main(int argc, char **argv)
{
	uint32_t msr;
	int fd;
	uint64_t *buf;
	size_t buf_sz;
	ssize_t ret;
	int num_cores;

	if (argc < 2) {
		printf("Usage: %s MSR\n", argv[0]);
		exit(-1);
	}
	msr = strtoul(argv[1], 0, 0);
	num_cores = get_num_pcores();
	fd = open("#arch/msr", O_RDWR);
	if (fd < 0) {
		perror("open");
		exit(-1);
	}
	buf_sz = num_cores * sizeof(uint64_t);
	buf = malloc(buf_sz);
	assert(buf);
	ret = pread(fd, buf, buf_sz, msr);
	if (ret < 0) {
		perror("pread");
		exit(-1);
	}
	for (int i = 0; i < num_cores; i++)
		printf("Core %3d, MSR 0x%08x: 0x%016llx\n", i, msr, buf[i]);
	return 0;
}
