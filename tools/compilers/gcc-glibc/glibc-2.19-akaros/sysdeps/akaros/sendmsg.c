/* Copyright (c) 2015-2016 Google Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * sendmsg(), on top of sendto(). */

#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>

/* In sendto.c */
ssize_t __sendto_iov(int fd, const struct iovec *iov, int iovcnt,
                     int flags, __CONST_SOCKADDR_ARG to, socklen_t tolen);

/* Send a message described MSG on socket FD.  Returns the number of bytes
 * sent, or -1 for errors.  */
ssize_t __sendmsg(int fd, const struct msghdr *msg, int flags)
{
	return __sendto_iov(fd, msg->msg_iov, msg->msg_iovlen, flags,
			    msg->msg_name, msg->msg_namelen);
}
weak_alias(__sendmsg, sendmsg)
