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
#include <iplib/iplib.h>
#include <netpacket/packet.h>

/* Given a list of ifas (possibly null), prepend ifas for ethernet devices.
 * Returns the new head of the list (also possibly null). */
static struct ifaddrs *get_ether_addrs(struct ifaddrs *ifa)
{
	struct ifaddrs *new_ifa;
	struct sockaddr_ll *sa_ll;
	DIR *net;
	struct dirent *d;
	int addr_fd;
	char path[MAX_PATH_LEN];
	/* 6 is known everywhere, defined nowhere. So is 6 * 2 */
	char etheraddr[12];
	#define SIZE_OF_ETHER 5

	net = opendir("/net");
	if (net == NULL)
		return ifa;

	for (d = readdir(net); d; d = readdir(net)) {
		if (strncmp(d->d_name, "ether", SIZE_OF_ETHER))
			continue;
		snprintf(path, sizeof(path), "/net/%s/addr", d->d_name);
		addr_fd = open(path, O_RDONLY);
		if (addr_fd < 0)
			continue;
		if (read(addr_fd, etheraddr, sizeof(etheraddr)) < sizeof(etheraddr)) {
			fprintf(stderr, "Read addr from %s: %r", d->d_name);
			close(addr_fd);
			continue;
		}
		/* getifaddrs is a stupid design as it only admits of
		 * one address per interface.  Don't even bother
		 * filling in ifa_{addr,netmask}. They're allowed to
		 * be NULL.  Broadaddr need be set IFF a bit is set
		 * in the flags field. We don't set either one.
		 */
		new_ifa = calloc(sizeof(*ifa), 1);
		new_ifa->ifa_next = ifa;
		ifa = new_ifa;
		ifa->ifa_name = strdup(d->d_name);
		sa_ll = calloc(sizeof(struct sockaddr_ll), 1);
		ifa->ifa_addr = (struct sockaddr*)sa_ll;
		sa_ll->sll_family = AF_PACKET;
		/* TODO: could set protocol and hatype, if we ever get the headers for
		 * the options.  Probably not worth it. */
		sa_ll->sll_ifindex = atoi(&ifa->ifa_name[SIZE_OF_ETHER]);
		sa_ll->sll_halen = 6;
		for (int i = 0; i < 6; i++)
			sscanf(&etheraddr[i * 2], "%02x", &sa_ll->sll_addr[i]);
		close(addr_fd);
	}
	closedir(net);
	return ifa;
}

/* Given a list of ifas (possibly null), prepend an ifa for the given lifc. */
static struct ifaddrs *get_lifc_addr(struct ifaddrs *ifa, struct iplifc *lifc)
{
	struct ifaddrs *new_ifa;
	struct sockaddr_in *sa_in, *mask_in;
	struct sockaddr_in6 *sa_in6, *mask_in6;

	if (!ipcmp(lifc->ip, IPnoaddr))
		return ifa;
	new_ifa = calloc(sizeof(*ifa), 1);
	new_ifa->ifa_next = ifa;
	ifa = new_ifa;
	ifa->ifa_name = NULL;
	if (isv4(lifc->ip)) {
		sa_in = calloc(sizeof(struct sockaddr_in), 1);
		sa_in->sin_family = AF_INET;
		ifa->ifa_addr = (struct sockaddr*)sa_in;
		v6tov4((uint8_t*)&sa_in->sin_addr, lifc->ip);

		mask_in = calloc(sizeof(struct sockaddr_in), 1);
		mask_in->sin_family = AF_INET;
		ifa->ifa_netmask = (struct sockaddr*)mask_in;
		/* The V4 mask is the last four bytes of the full v6 mask */
		memcpy((uint8_t*)&mask_in->sin_addr, &lifc->mask[12],
		       sizeof(struct in_addr));
	} else {
		/* TODO: check this when we have a v6 system */
		sa_in6 = calloc(sizeof(struct sockaddr_in6), 1);
		sa_in6->sin6_family = AF_INET6;
		ifa->ifa_addr = (struct sockaddr*)sa_in6;
		memcpy(&sa_in6->sin6_addr, lifc->ip, sizeof(struct in6_addr));

		mask_in6 = calloc(sizeof(struct sockaddr_in6), 1);
		mask_in6->sin6_family = AF_INET6;
		ifa->ifa_netmask = (struct sockaddr*)mask_in6;
		memcpy((uint8_t*)&mask_in6->sin6_addr, lifc->mask,
		       sizeof(struct in6_addr));
	}
	return ifa;
}

/* Given a list of ifas (possibly null), prepend ifas for inet devices.
 * Returns the new head of the list (also possibly null). */
static struct ifaddrs *get_inet_addrs(struct ifaddrs *ifa)
{
	struct ipifc *ifc, *ifc_i;
	struct iplifc *lifc_i;

	/* This gives us a list of ipifcs (actual interfaces), each of which has a
	 * list of lifcs (local interface, including the IP addr). */
	ifc = readipifc(NULL, NULL, -1);
	for (ifc_i = ifc; ifc_i; ifc_i = ifc_i->next) {
		for (lifc_i = ifc_i->lifc; lifc_i; lifc_i = lifc_i->next)
			ifa = get_lifc_addr(ifa, lifc_i);
	}
	free_ipifc(ifc);
	return ifa;
}

int getifaddrs(struct ifaddrs **ifap)
{
	struct ifaddrs *ifa = NULL;

	*ifap = NULL;
	ifa = get_ether_addrs(ifa);
	ifa = get_inet_addrs(ifa);
	*ifap = ifa;
	return 0;
}

void freeifaddrs(struct ifaddrs *ifa)
{
	struct ifaddrs *next = ifa;

	while (ifa) {
		next = ifa->ifa_next;

		free(ifa->ifa_name);
		free(ifa->ifa_addr);
		free(ifa->ifa_netmask);
		free(ifa);

		ifa = next;
	}
}
