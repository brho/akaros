/* This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file. */

#include <stdlib.h>
#include <stdio.h>
#include <parlib/parlib.h>
#include <unistd.h>
#include <signal.h>
#include <iplib/iplib.h>
#include <iplib/icmp.h>
#include <ctype.h>
#include <pthread.h>
#include <parlib/spinlock.h>
#include <parlib/timing.h>
#include <parlib/tsc-compat.h>
#include <parlib/printf-ext.h>
#include <parlib/alarm.h>
#include <ndblib/ndb.h>
#include <ifaddrs.h>

int main(int argc, char **argv)
{
	int i, naddr, o;
	uint8_t *cp;
	struct ifaddrs *ifa;

	naddr = getifaddrs(&ifa);

	for (naddr = 0; ifa; ifa = ifa->ifa_next, naddr++) {
		printf("%s: ", ifa->ifa_name);
		cp = ifa->ifa_data;
		for (o = 0; o < 6; o++) {
			printf("%02x", cp[o]);
			if (o < 5)
				printf(":");
		}
		printf("\n");
	}
	printf("%d ifaddrs\n", naddr);
	freeifaddrs(ifa);
}
