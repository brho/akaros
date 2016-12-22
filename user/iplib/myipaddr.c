/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */
#define _GNU_SOURCE
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

/* Finds the default router from net/iproute, stores the full IP address
 * (IPaddrlen) in addr.  Returns 0 on success. */
int my_router_addr(uint8_t *addr, char *net)
{
	char *line = NULL;
	size_t len = 0;
	ssize_t read;
	FILE *fp;
	char *p, *str;
	uint8_t ipaddr[IPaddrlen];
	uint8_t v4addr[IPaddrlen];
	char netpath[MAX_PATH_LEN];
	int ret = -1;

	if (!net)
		net = "/net";
	snprintf(netpath, sizeof(netpath), "%s/iproute", net);
	fp = fopen(netpath, "r");
	if (!fp)
		return -1;
	while ((read = getline(&line, &len, fp)) != -1) {
		p = strchr(line, ' ');
		if (!p) {
			werrstr("Malformed line, no initial space");
			goto out;
		}
		*p++ = 0;
		parseip(ipaddr, line);
		if (isv4(ipaddr)) {
			v6tov4(v4addr, ipaddr);
			if (!equivip4(v4addr, IPnoaddr))
				continue;
		} else {
			if (!equivip6(ipaddr, IPnoaddr))
				continue;
		}
		p = strchr(p, ' ');
		if (!p) {
			werrstr("Malformed line, no second space %s", line);
			goto out;
		}
		p++;
		str = p;
		p = strchr(p, ' ');
		if (!p) {
			werrstr("Malformed line, no third space %s", line);
			goto out;
		}
		*p++ = 0;
		parseip(addr, str);
		ret = 0;
	}
out:
	free(line);
	fclose(fp);
	return ret;
}
