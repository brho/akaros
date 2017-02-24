/* Copyright (c) 2016 Google Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * shutdown(). */

#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <sys/plan9_helpers.h>

/* Shuts down all or part of the connection open on socket FD.  Returns 0 on
 * success, -1 for errors. */
int shutdown(int fd, int how)
{
	int ret, ctlfd;
	static const char rd_msg[] = "shutdown rd";
	static const char wr_msg[] = "shutdown wr";
	static const char rdwr_msg[] = "shutdown rdwr";
	char *msg;
	size_t msg_sz;
	Rock *r;

	switch (how) {
	case SHUT_RD:
		msg = rd_msg;
		msg_sz = sizeof(rd_msg);
		break;
	case SHUT_WR:
		msg = wr_msg;
		msg_sz = sizeof(wr_msg);
		break;
	case SHUT_RDWR:
		msg = rdwr_msg;
		msg_sz = sizeof(rdwr_msg);
		break;
	default:
		errno = EINVAL;
		werrstr("shutdown has bad 'how' %d", how);
		return -1;
	}
	r = _sock_findrock(fd, 0);
	if (!r) {
		errno = EBADF;
		werrstr("Rock lookup failed");
		return -1;
	}
	ctlfd = _sock_open_ctlfd(r);
	if (!ctlfd)
		return -1;
	ret = write(ctlfd, msg, msg_sz);
	close(ctlfd);
	if (ret != msg_sz)
		return -1;
	return 0;
}
