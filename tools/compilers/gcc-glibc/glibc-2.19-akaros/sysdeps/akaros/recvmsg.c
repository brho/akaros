/* Copyright (c) 2015 Google Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * recvmsg(), on top of recvfrom(). */

#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>

/* In recvfrom.c */
extern ssize_t __recvfrom_iov(int fd, const struct iovec *iov, int iovcnt,
                              int flags, __SOCKADDR_ARG from,
                              socklen_t * __restrict fromlen);

/* Receive a message as described by MSG from socket FD.  Returns the number of
 * bytes read or -1 for errors.  */
ssize_t __recvmsg(int fd, struct msghdr *msg, int flags)
{
	ssize_t ret;

	ret = __recvfrom_iov(fd, msg->msg_iov, msg->msg_iovlen,
	                     flags, msg->msg_name, &msg->msg_namelen);
	if (ret == -1)
		return ret;
	/* On successful calls, there's extra info we can return via *msg */
	msg->msg_controllen = 0;
	msg->msg_flags = 0;
	return ret;
}
weak_alias(__recvmsg, recvmsg)
