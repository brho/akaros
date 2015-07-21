/* Copyright (c) 2015 Google, Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details. */

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <ros/syscall.h>

#include "plan9_sockets.h"

/* O_NONBLOCK is handled in userspace for conversations set up with socket
 * shims for compatibility with existing code.  9ns code should not use fcntl to
 * attempt to set nonblock.
 *
 * Note that FDs that resulted from open of a clone with O_NONBLOCK will still
 * have O_NONBLOCK set on the chan for the ctl, but only for the first chan that
 * represents ctl.  So some 9ns code can see O_NONBLOCK from getfl. */

static int toggle_nonblock(Rock *r)
{
	int ret, ctlfd;
	char on_msg[] = "nonblock on";
	char off_msg[] = "nonblock off";

	ctlfd = open(r->ctl, O_RDWR);
	if (!ctlfd)
		return -1;
	if (r->sopts & SOCK_NONBLOCK) {
		ret = write(ctlfd, off_msg, sizeof(off_msg));
		if (ret < 0)
			goto out;
		r->sopts &= ~SOCK_NONBLOCK;
	} else {
		ret = write(ctlfd, on_msg, sizeof(on_msg));
		if (ret < 0)
			goto out;
		r->sopts |= SOCK_NONBLOCK;
	}
	ret = 0;
out:
	close(ctlfd);
	return ret;
}

/* Sets the fd's nonblock status IAW arg's settings.  Returns 0 on success.
 * Modifies *arg such that the new version is the one that should be passed to
 * the kernel for setfl. */
static int set_nonblock_status(int fd, int *arg)
{
	int ret = 0;
	Rock *r = _sock_findrock(fd, 0);
	if (!r) {
		/* We don't clear O_NONBLOCK, so non-sockets will still pass it to the
		 * kernel and probably error out */
		return 0;
	}
	/* XOR of two flag tests, checking if we need to change anything */
	if (!(r->sopts & SOCK_NONBLOCK) != !(*arg & O_NONBLOCK))
		ret = toggle_nonblock(r);
	/* Either way, we clear O_NONBLOCK, so we don't ask the kernel to set it */
	*arg &= ~O_NONBLOCK;
	return ret;
}

/* If fd is a nonblocking socket, this returns O_NONBLOCK */
static int get_nonblock_status(int fd)
{
	Rock *r = _sock_findrock(fd, 0);
	if (!r)
		return 0;
	return r->sopts & SOCK_NONBLOCK ? O_NONBLOCK : 0;
}

/* in sysdeps/akaros/fcntl.c */
extern int __vfcntl(int fd, int cmd, va_list vl);

int fcntl(int fd, int cmd, ...)
{
	int ret, arg;
	va_list vl;
	va_start(vl, cmd);
	switch (cmd) {
		case F_GETFL:
			ret = ros_syscall(SYS_fcntl, fd, cmd, 0, 0, 0, 0);
			if (ret != -1)
				ret |= get_nonblock_status(fd);
			break;
		case F_SETFL:
			arg = va_arg(vl, int);
			if ((ret = set_nonblock_status(fd, &arg)))
				return ret;
			ret = ros_syscall(SYS_fcntl, fd, cmd, arg, 0, 0, 0);
			break;
		default:
			ret = __vfcntl(fd, cmd, vl);
	}
	va_end(vl);
	return ret;
}
