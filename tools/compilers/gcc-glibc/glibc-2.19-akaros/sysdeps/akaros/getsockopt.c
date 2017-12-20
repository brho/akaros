/* Copyright (c) 2015 Google, Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details. */

#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <sys/plan9_helpers.h>

static int sol_socket_error_gso(Rock *r, void *optval, socklen_t *optlen)
{
	char buf[Ctlsize];
	int fd, ret;
	char *p;

	_sock_get_conv_filename(r, "status", buf);
	fd = open(buf, O_RDONLY);
	if (fd < 0)
		return -1;
	ret = read(fd, buf, sizeof(buf));
	close(fd);
	if (ret < 0)
		return -1;
	p = strchr(buf, ' ');
	if (!p)
		return -1;
	*p = 0;
	/* The first word in a connected TCP conv status file is 'Established'.  For
	 * UDP it is 'Open'.
	 *
	 * For now, we'll default to no socket error, and only set the error if we
	 * know we aren't Established/Open.  If we want, we can parse the different
	 * string values, like Established, Syn_sent, and return custom error
	 * messages.  But just ECONNREFUSED is fine for now. */
	ret = 0;
	switch (r->stype) {
	case SOCK_DGRAM:
		if (strcmp(buf, "Open"))
			ret = ECONNREFUSED;
		break;
	case SOCK_STREAM:
		if (strcmp(buf, "Established"))
			ret = ECONNREFUSED;
		break;
	}
	*(int*)optval = ret;
	*optlen = 4;
	return 0;
}

static int sol_socket_gso(Rock *r, int optname, void *optval, socklen_t *optlen)
{
	switch (optname) {
		case (SO_TYPE):
			if (*optlen < 4) {
				__set_errno(EINVAL);
				return -1;
			}
			*(int*)optval = r->stype;
			*optlen = 4;
			break;
		case (SO_ERROR):
			return sol_socket_error_gso(r, optval, optlen);
		default:
			__set_errno(ENOPROTOOPT);
			return -1;
	};
	return 0;
}

int __getsockopt(int sockfd, int level, int optname, void *optval,
                 socklen_t *optlen)
{
	Rock *r = _sock_findrock(sockfd, 0);
	if (!r) {
		/* could be EBADF too, we can't tell */
		__set_errno(ENOTSOCK);
		return -1;
	}
	switch (level) {
		case (SOL_SOCKET):
			return sol_socket_gso(r, optname, optval, optlen);
		default:
			__set_errno(ENOPROTOOPT);
			return -1;
	};
}
weak_alias(__getsockopt, getsockopt)
