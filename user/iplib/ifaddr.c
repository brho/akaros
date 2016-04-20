#define _LARGEFILE64_SOURCE /* needed to use lseek64 */

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <parlib/arch/arch.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>

#include <dirent.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <ros/syscall.h>
#include <ros/fs.h>

int getifaddrs(struct ifaddrs **ifap)
{
	DIR *net;
	struct dirent *d;
	int addr;
	/* 6 is known everywhere, defined nowhere. So is 6 * 2 */
	char etheraddr[12];
	struct ifaddrs *ifa = NULL;
	uint8_t *v;
	int i;

	*ifap = NULL;

	net = opendir("/net");
	if (net == NULL) {
		fprintf(stderr, "/net: %r");
		return -1;
	}

	for (d = readdir(net); d; d = readdir(net)) {
		/*
		 * For now we only do ethernet MACs.  It's easy to
		 * change later: just get rid of the following 2
		 * lines.
		 */
		if (strncmp(d->d_name, "ether", 5))
			continue;
		sprintf(etheraddr, "/net/%s/addr", d->d_name);
		addr = open(etheraddr, O_RDONLY);
		if (addr < 0)
			continue;
		if (read(addr, etheraddr, sizeof(etheraddr)) < sizeof(etheraddr)) {
			fprintf(stderr, "Read addr from %s: %r", d->d_name);
			close(addr);
			continue;
		}
		/* getifaddrds is a stupid design as it only admits of
		 * one address per interface.  Don't even bother
		 * filling in ifa_{addr,netmask}. They're allowed to
		 * be NULL.  Broadaddr need be set IFF a bit is set
		 * in the flags field. We don't set either one.
		 */
		if (!ifa) {
			ifa = calloc(sizeof(*ifa), 1);
			*ifap = ifa;
		} else {
			ifa->ifa_next = calloc(sizeof(*ifa), 1);
			ifa = ifa->ifa_next;
		}
		ifa->ifa_name = strdup(d->d_name);
		/*
		 * 6 is known everywhere, not defined anywhere.  oh
		 * yeah, another binary thing. 1976 all over again.
		 */
		v = malloc(6);
		for (i = 0; i < 6; i++)
			sscanf(&etheraddr[i*2], "%02x", &v[i]);
		ifa->ifa_data = v;
		close(addr);
	}
	closedir(net);
	return 0;
}

void freeifaddrs(struct ifaddrs *ifa)
{
	for (; ifa; ifa = ifa->ifa_next)
		free(ifa);
}
