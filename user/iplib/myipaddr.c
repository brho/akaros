/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */
#include <stdlib.h>

#include <iplib/iplib.h>
#include <parlib/parlib.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

// find first ip addr that isn't the friggin loopback address
// unless there are no others
int myipaddr(uint8_t *ip, char *net)
{
	struct iplifc *lifc;
	struct ipifc *ifc;

	lifc = get_first_noloop_iplifc(net, &ifc);
	if (!lifc) {
		ipmove(ip, IPnoaddr);
		return -1;
	}
	ipmove(ip, lifc->ip);
	free_ipifc(ifc);
	return 0;
}
