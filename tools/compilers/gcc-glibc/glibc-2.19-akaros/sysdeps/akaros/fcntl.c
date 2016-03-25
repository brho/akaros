/* Copyright (c) 2015 Google, Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details. */

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <ros/syscall.h>

int __vfcntl(int fd, int cmd, va_list vl)
{
	int ret, arg, advise;
	__off64_t offset, len;
	switch (cmd) {
		case F_GETFD:
		case F_SYNC:
			ret = ros_syscall(SYS_fcntl, fd, cmd, 0, 0, 0, 0);
			break;
		case F_DUPFD:
		case F_SETFD:
		case F_GETFL:
		case F_SETFL:
			arg = va_arg(vl, int);
			ret = ros_syscall(SYS_fcntl, fd, cmd, arg, 0, 0, 0);
			break;
		case F_ADVISE:
			offset = va_arg(vl, __off64_t);
			len = va_arg(vl, __off64_t);
			advise = va_arg(vl, int);
			ret = ros_syscall(SYS_fcntl, fd, cmd, offset, len, advise, 0);
			break;
		default:
			errno = ENOSYS;
			ret = -1;
	}
	return ret;
}

/* We used to override fcntl() with some Rock processing from within Glibc.  It
 * might be useful to overrider fcntl() in the future. */
int __fcntl(int fd, int cmd, ...)
{
	int ret;
	va_list vl;
	va_start(vl, cmd);
	ret = __vfcntl(fd, cmd, vl);
	va_end(vl);
	return ret;
}
libc_hidden_def(__fcntl)
weak_alias(__fcntl, fcntl)
