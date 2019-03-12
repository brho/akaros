/* 
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>

/* bsd extensions */
#include <sys/uio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

#include <sys/plan9_helpers.h>

int __getservbyport_r(int port, const char *proto, struct servent *result_buf,
                      char *buf, size_t buflen, struct servent **result)
{

	char port_buf[32];

	snprintf(port_buf, sizeof(port_buf), "%d", port);
	/* the plan 9 version can handle a port or a name */
	return getservbyname_r(port_buf, proto, result_buf, buf, buflen,
			       result);
}
weak_alias(__getservbyport_r, getservbyport_r);
