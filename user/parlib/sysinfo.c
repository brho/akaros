/* Copyright (c) 2016 Google Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Helper functions to query information about the system. */

#include <parlib/stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>
#include <unistd.h>

#include <parlib/sysinfo.h>
#include <ros/arch/arch.h>

int get_num_pcores(void)
{
	int fd;
	int ret;
	char buf[128];

	fd = open("#vars/num_cores!dw", O_RDONLY);
	if (fd < 0)
		return MAX_NUM_CORES;
	if (read(fd, buf, sizeof(buf)) < 0) {
		/* major bug */
		perror("#vars read");
		exit(-1);
	}
	ret = strtol(buf, 0, 0);
	close(fd);
	return ret;
}
