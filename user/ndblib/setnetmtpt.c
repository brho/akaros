/* 
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */
#include <stdlib.h>

#include <stdio.h>
#include <parlib.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <iplib.h>
#include <ndb.h>

void
setnetmtpt(char *net, int n, char *x)
{
	if(x == NULL)
		x = "/net";

	if(*x == '/'){
		strncpy(net, x, n);
		net[n-1] = 0;
	} else {
		snprintf(net, n, "/net%s", x);
	}
}
