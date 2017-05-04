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
#include <netpacket/packet.h>
#include <arpa/inet.h>

static void print_eth(struct ifaddrs *ifa)
{
	struct sockaddr_ll *sa_ll = (struct sockaddr_ll*)ifa->ifa_addr;

	printf("\tAddr: ");
	for (int i = 0; i < sa_ll->sll_halen; i++) {
		printf("%02x", sa_ll->sll_addr[i]);
		if (i < sa_ll->sll_halen - 1)
			printf(":");
	}
	printf("\n");
	printf("\tNIC %d\n", sa_ll->sll_ifindex);
}

static void print_inet(struct ifaddrs *ifa)
{
	struct sockaddr_in *sa_in = (struct sockaddr_in*)ifa->ifa_addr;
	struct sockaddr_in *mask_in = (struct sockaddr_in*)ifa->ifa_netmask;
	char buf[INET_ADDRSTRLEN];

	printf("\tAddr: %s\n", inet_ntop(AF_INET, &sa_in->sin_addr, buf,
	                                 sizeof(buf)));
	if (mask_in)
		printf("\tMask: %s\n", inet_ntop(AF_INET, &mask_in->sin_addr, buf,
		                                 sizeof(buf)));
}

static void print_inet6(struct ifaddrs *ifa)
{
	struct sockaddr_in6 *sa_in6 = (struct sockaddr_in6*)ifa->ifa_addr;
	char buf[INET6_ADDRSTRLEN];

	printf("\tAddr: %s\n", inet_ntop(AF_INET6, &sa_in6->sin6_addr, buf,
	                                 sizeof(buf)));
}

int main(int argc, char **argv)
{
	int family;
	struct ifaddrs *ifaddrs, *ifa;

	if (getifaddrs(&ifaddrs) != 0) {
		perror("getifaddr");
		exit(-1);
	}

	for (ifa = ifaddrs; ifa; ifa = ifa->ifa_next) {
		printf("%s: ", ifa->ifa_name);
		if (!ifa->ifa_addr) {
			printf("No addr\n");
			continue;
		} else {
			printf("\n");
		}
		family = ifa->ifa_addr->sa_family;
		printf("\tFamily: %s\n", (family == AF_PACKET) ? "AF_PACKET" :
		                         (family == AF_INET) ? "AF_INET" :
		                         (family == AF_INET6) ? "AF_INET6" :
		                         "Unknown");
		switch (family) {
		case AF_PACKET:
			print_eth(ifa);
			break;
		case AF_INET:
			print_inet(ifa);
			break;
		case AF_INET6:
			print_inet6(ifa);
			break;
		}
	}
	freeifaddrs(ifaddrs);
}
