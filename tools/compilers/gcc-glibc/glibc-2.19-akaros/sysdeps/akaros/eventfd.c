/* Copyright (c) 2015 Google Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Implementation of glibc's eventfd interface, hooking in to #eventfd */

#include <sys/eventfd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/plan9_helpers.h>

/* Gets a new EFD instance, returning the FD on success. */
int eventfd(int initval, int flags)
{
	const char *pathname = "#eventfd/efd";
	int oflags = 0;
	int efd;

	/* we choose semaphore mode by passing 'sem' as the device spec */
	if (flags & EFD_SEMAPHORE)
		pathname = "#eventfd.sem/efd";
	if (flags & EFD_CLOEXEC)
		oflags |= O_CLOEXEC;
	if (flags & EFD_NONBLOCK)
		oflags |= O_NONBLOCK;
	efd = open(pathname, O_READ | O_WRITE | oflags);
	if (efd < 0)
		return efd;
	/* default initval for an #eventfd is 0. */
	if (initval) {
		if (eventfd_write(efd, initval)) {
			close(efd);
			return -1;
		}
	}
	return efd;
}

/* Reads into *value, in host-endian format, from the efd.  Returns 0 on
 * success.  */
int eventfd_read(int efd, eventfd_t *value)
{
	int ret;
	char num64[32];
	ret = read(efd, num64, sizeof(num64));
	if (ret <= 0)
		return -1;
	*value = strtoul(num64, 0, 0);
	return 0;
}

/* Writes value, in host-endian format, into the efd.  Returns 0 on success. */
int eventfd_write(int efd, eventfd_t value)
{
	return write_hex_to_fd(efd, value);
}
