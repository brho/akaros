/* Copyright (c) 2015 Google, Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details. */

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <ros/syscall.h>

/* in sysdeps/akaros/fcntl.c */
extern int __vfcntl(int fd, int cmd, va_list vl);

int fcntl(int fd, int cmd, ...)
{
	int ret;
	va_list vl;
	va_start(vl, cmd);
	ret = __vfcntl(fd, cmd, vl);
	va_end(vl);
	return ret;
}
