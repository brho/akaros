/* Copyright (c) 2015 Google, Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details. */

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <ros/syscall.h>

/* This is overridden when we're building with sockets.  We need to go through
 * these hoops since fnctl is a lower-level glibc function and is included in
 * code that can't use sockets/rocks/stdio. */
static void __weak_sock_mirror_fcntl(int sock_fd, int cmd, long arg)
{
}
weak_alias(__weak_sock_mirror_fcntl, _sock_mirror_fcntl);

int __vfcntl(int fd, int cmd, va_list vl)
{
	int ret, arg, advise;
	__off64_t offset, len;
	long a1, a2, a3, a4;

	switch (cmd) {
	case F_GETFD:
	case F_SYNC:
		ret = ros_syscall(SYS_fcntl, fd, cmd, 0, 0, 0, 0);
		break;
	case F_DUPFD:
	case F_SETFD:
	case F_GETFL:
		arg = va_arg(vl, int);
		ret = ros_syscall(SYS_fcntl, fd, cmd, arg, 0, 0, 0);
		break;
	case F_SETFL:
		arg = va_arg(vl, int);
		ret = ros_syscall(SYS_fcntl, fd, cmd, arg, 0, 0, 0);
		/* For SETFL, we mirror the operation on all of the Rocks FDs.
		 * If the others fail, we won't hear about it.  Similarly, we
		 * only GETFL for the data FD. */
		_sock_mirror_fcntl(fd, cmd, arg);
		break;
	case F_ADVISE:
		offset = va_arg(vl, __off64_t);
		len = va_arg(vl, __off64_t);
		advise = va_arg(vl, int);
		ret = ros_syscall(SYS_fcntl, fd, cmd, offset, len, advise, 0);
		break;
	default:
		/* We don't know the number of arguments for generic calls.
		 * We'll just yank whatever arguments there could be from the
		 * ABI and send them along. */
		a1 = va_arg(vl, long);
		a2 = va_arg(vl, long);
		a3 = va_arg(vl, long);
		a4 = va_arg(vl, long);
		ret = ros_syscall(SYS_fcntl, fd, cmd, a1, a2, a3, a4);
		break;
	}
	return ret;
}

/* We used to override fcntl() with some Rock processing from within Glibc.  It
 * might be useful to override fcntl() in the future. */
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
