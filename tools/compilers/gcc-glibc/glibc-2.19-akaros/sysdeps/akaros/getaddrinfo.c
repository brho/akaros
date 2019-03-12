/* Copyright (c) 2015 Google, Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * getaddrinfo/freeaddrinfo.
 *
 * Supports IPv4, TCP, UDP, and maybe a couple protocols/socktypes.
 *
 * Doesn't handle service resolution yet, and only returns one addrinfo.
 */

#include <netdb.h>
#include <malloc.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>

/* Helper, given the serv string, figures out the port and protocol.  Returns 0
 * on success or an EAI_ error. */
static int serv_get_portprotocol(const char *serv, unsigned long *port_ret,
                                 int *protocol_ret, struct addrinfo *hints)
{
	char *strtoul_end = 0;
	unsigned long port = 0;	/* uninitialized, up to the main caller */
	int protocol = 0;	/* any protocol, will assume TCP/UDP later */

	if (serv) {
		if (serv[0] == '\0')
			return EAI_NONAME;
		port = strtoul(serv, &strtoul_end, 0);
		/* Already checked that serv != \0 */
		if (*strtoul_end != '\0') {
			if (hints->ai_flags & AI_NUMERICSERV)
				return EAI_NONAME;
			/* CS lookup */
			/* TODO: get a port, maybe a protocol.  If we have a
			 * restriction on the protocol from hints, check that
			 * here.  Probably need to rework this a bit if we have
			 * multiple protocols. */
			return EAI_NONAME;
		}
	}
	*port_ret = port;
	*protocol_ret = protocol;
	return 0;
}

/* Helper: fills in all of the addr related info in ai for an ipv4 addr/port.
 * addr is in network order.  port is in host order. */
static void addrinfo_filladdr_ipv4(struct addrinfo *ai, in_addr_t addr,
                                   uint16_t port)
{
	struct sockaddr_in *sa_in4 = (struct sockaddr_in*)ai->ai_addr;
	ai->ai_family = AF_INET;
	ai->ai_addrlen = sizeof(struct sockaddr_in);
	sa_in4->sin_family = AF_INET;
	sa_in4->sin_addr.s_addr = addr;
	sa_in4->sin_port = htons(port);
}

static int resolve_name_to_ipv4(const char *name, struct in_addr *hostaddr)
{
	struct hostent host, *result;
	char buf[1024];
	int _herrno;
	int ret;
	ret = gethostbyname2_r(name, AF_INET, &host, buf, sizeof(buf),
	                       &result, &_herrno);
	/* TODO: deal with the herrno errors and errno errors. */
	if (ret)
		return -1;
	assert(result == &host);
	memcpy(hostaddr, host.h_addr_list[0], host.h_length);
	return 0;
}

/* Given the node string, fills in the addr related info in ai: the ai_family,
 * ai_addr, and ai_addrlen.  The ai_addr needs the port too, though that's a big
 * ugly.  We only support ipv4, so there's just one of these.  Returns 0 on
 * success, EAI_ error o/w. */
static int node_fill_addrinfo(const char *node, struct addrinfo *ai,
                              uint16_t port, struct addrinfo *hints)
{
	struct in_addr local_addr;
	if (!node) {
		/* AI_PASSIVE only matters with no node name */
		if (hints->ai_flags & AI_PASSIVE) {
			/* Caller wants to bind */
			addrinfo_filladdr_ipv4(ai, INADDR_ANY, port);
		} else {
			/* Caller wants to send from localhost */
			addrinfo_filladdr_ipv4(ai, INADDR_LOOPBACK, port);
		}
	} else {
		if (inet_pton(AF_INET, node, &local_addr) == 1) {
			addrinfo_filladdr_ipv4(ai, local_addr.s_addr, port);
		} else {
			if (hints->ai_flags & AI_NUMERICHOST)
				return EAI_NONAME;
			if (resolve_name_to_ipv4(node, &local_addr))
				return EAI_FAIL;
			addrinfo_filladdr_ipv4(ai, local_addr.s_addr, port);
		}
	}
	return 0;
}

static int proto_to_socktype(int protocol)
{
	switch (protocol) {
	case IPPROTO_IP:
	case IPPROTO_TCP:
		return SOCK_STREAM;
	case IPPROTO_ICMP:
	case IPPROTO_UDP:
		return SOCK_DGRAM;
	case IPPROTO_RAW:
		return SOCK_RAW;
	}
	return -1;
}

static int socktype_to_proto(int socktype)
{
	switch (socktype) {
	case SOCK_STREAM:
		return IPPROTO_TCP;
	case SOCK_DGRAM:
		return IPPROTO_UDP;
	case SOCK_RAW:
		return IPPROTO_RAW;
	}
	return -1;
}

int getaddrinfo(const char *node, const char *serv,
                const struct addrinfo *hints, struct addrinfo **res)
{
	struct addrinfo *ai;
	struct addrinfo local_hints;

	local_hints.ai_socktype = 0;
	local_hints.ai_protocol = 0;
	local_hints.ai_family = AF_UNSPEC;
	local_hints.ai_flags = AI_V4MAPPED | AI_ADDRCONFIG;

	unsigned long port = 0;	/* AF agnostic, hope a long is enough */
	int protocol = 0;
	int ret;

	if (hints)
		local_hints = *hints;

	if (!node && !serv)
		return EAI_NONAME;

	/* Only support IPv4 for now */
	if ((local_hints.ai_family != AF_UNSPEC) &&
	    (local_hints.ai_family != AF_INET))
		return EAI_FAMILY;

	ai = malloc(sizeof(struct addrinfo));
	memset(ai, 0, sizeof(struct addrinfo));
	ai->ai_addr = malloc(sizeof(struct sockaddr_storage));
	memset(ai->ai_addr, 0, sizeof(struct sockaddr_storage));

	/* Only supporting TCP and UDP for now.  If protocol is 0, later we'll
	 * make addrinfos for both (TODO).  Likewise, we only support DGRAM or
	 * STREAM for socktype. */
	if ((ret = serv_get_portprotocol(serv, &port, &protocol, &local_hints)))
	{
		freeaddrinfo(ai);
		return ret;
	}
	if ((ret = node_fill_addrinfo(node, ai, port, &local_hints))) {
		freeaddrinfo(ai);
		return ret;
	}
	/* We have a mostly full addrinfo.  Still missing ai_protocol, socktype,
	 * flags (already 0) and canonname (already 0).
	 *
	 * We might have restrictions on our protocol from the hints or from
	 * what serv specifies.  If we don't have a protocol yet (== 0), that
	 * means we have no restrictions from serv. */
	if (local_hints.ai_protocol) {
		if (protocol && protocol != local_hints.ai_protocol) {
			/* requested protocol wasn't available */
			freeaddrinfo(ai);
			return EAI_SERVICE;
		}
		ai->ai_protocol = local_hints.ai_protocol;
	} else if (protocol) {
		ai->ai_protocol = protocol;
	} else if (local_hints.ai_socktype) {
		ai->ai_protocol = socktype_to_proto(local_hints.ai_socktype);
	} else {
		/* no serv restrictions, no preferences */
		ai->ai_protocol = IPPROTO_TCP;
		/* TODO: consider building a second addrinfo for UDP.  while
		 * you're at it, support IPv6 and a bunch of other options! */
	}
	if (ai->ai_protocol == -1) {
		freeaddrinfo(ai);
		return EAI_SERVICE;
	}
	ai->ai_socktype = proto_to_socktype(ai->ai_protocol);
	if (local_hints.ai_socktype &&
	    (local_hints.ai_socktype != ai->ai_socktype)) {
		/* they requested a socktype, but we can't handle it */
		freeaddrinfo(ai);
		return EAI_SOCKTYPE;
	}
	/* TODO: support AI_CANONNAME, only in the first ai */
	*res = ai;
	return 0;
}
libc_hidden_def(getaddrinfo)

void freeaddrinfo(struct addrinfo *ai)
{
	struct addrinfo *next;

	if (!ai)
		return;
	free(ai->ai_addr);
	free(ai->ai_canonname);
	next = ai->ai_next;
	free(ai);
	freeaddrinfo(next);
}
libc_hidden_def(freeaddrinfo)
