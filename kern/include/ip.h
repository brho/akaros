/*
 * Copyright 2013 Google Inc.
 * Copyright (c) 1989-2003 by Lucent Technologies, Bell Laboratories.
 */
struct ipifc;
struct fs;

enum {
	Addrlen = 64,
	Maxproto = 20,
	Nhash = 64,
	Maxincall = 128,
	Nchans = 1024,
	MAClen = 16,	/* longest mac address */

	MAXTTL = 255,
	DFLTTOS = 0,

	IPaddrlen = 16,
	IPv4addrlen = 4,
	IPv4off = 12,
	IPllen = 4,

	/* ip versions */
	V4 = 4,
	V6 = 6,
	IP_VER4 = 0x40,
	IP_VER6 = 0x60,
	IP_HLEN4 = 5,	/* v4: Header length in words */
	/* barf. Temporary. */
#ifndef IP_DF
	IP_DF = 0x4000,	/* v4: Don't fragment */
	IP_MF = 0x2000,	/* v4: More fragments */
#endif
	IP4HDR = 20,	/* sizeof(struct Ip4hdr) */
	IP_MAX = 64 * 1024,	/* Max. Internet packet size, v4 & v6 */

	/* 2^Lroot trees in the root table */
	Lroot = 10,

	Maxpath = 64,
};

enum {
	Idle = 0,
	Announcing = 1,
	Announced = 2,
	Connecting = 3,
	Connected = 4,
};

/* MIB II counters */
enum {
	Forwarding,
	DefaultTTL,
	InReceives,
	InHdrErrors,
	InAddrErrors,
	ForwDatagrams,
	InUnknownProtos,
	InDiscards,
	InDelivers,
	OutRequests,
	OutDiscards,
	OutNoRoutes,
	ReasmTimeout,
	ReasmReqds,
	ReasmOKs,
	ReasmFails,
	FragOKs,
	FragFails,
	FragCreates,

	Nipstats,
};

/* logical interface associated with a physical one */
struct iplifc {
	uint8_t local[IPaddrlen];
	uint8_t mask[IPaddrlen];
	uint8_t remote[IPaddrlen];
	uint8_t net[IPaddrlen];
	uint8_t tentative;			/* =1 => v6 dup disc on, =0 => confirmed unique */
	uint8_t onlink;				/* =1 => onlink, =0 offlink. */
	uint8_t autoflag;			/* v6 autonomous flag */
	long validlt;				/* v6 valid lifetime */
	long preflt;				/* v6 preferred lifetime */
	long origint;				/* time when addr was added */
	struct Iplink *link;		/* addresses linked to this lifc */
	struct iplifc *next;
};

/* binding twixt struct Ipself and struct iplifc */
struct Iplink {
	struct Ipself *self;
	struct iplifc *lifc;
	struct Iplink *selflink;	/* next link for this local address */
	struct Iplink *lifclink;	/* next link for this ifc */
	uint32_t expire;
	struct Iplink *next;		/* free list */
	struct kref ref;
};

/* rfc 2461, pp.40—43. */

/* default values, one per stack */
struct routerparams {
	int mflag;					/* flag: managed address configuration */
	int oflag;					/* flag: other stateful configuration */
	int maxraint;				/* max. router adv interval (ms) */
	int minraint;				/* min. router adv interval (ms) */
	int linkmtu;				/* mtu options */
	int reachtime;				/* reachable time */
	int rxmitra;				/* retransmit interval */
	int ttl;					/* cur hop count limit */
	int routerlt;				/* router lifetime */
};

struct hostparams {
	int rxmithost;
};

/*
 * Internet Protocol Version 6
 *
 * rfc2460 defines the protocol, rfc2461 neighbour discovery, and
 * rfc2462 address autoconfiguration.  rfc4443 defines ICMP; was rfc2463.
 * rfc4291 defines the address architecture (including prefices), was rfc3513.
 * rfc4007 defines the scoped address architecture.
 *
 * global unicast is anything but unspecified (::), loopback (::1),
 * multicast (ff00::/8), and link-local unicast (fe80::/10).
 *
 * site-local (fec0::/10) is now deprecated, originally by rfc3879.
 *
 * Unique Local IPv6 Unicast Addresses are defined by rfc4193.
 * prefix is fc00::/7, scope is global, routing is limited to roughly a site.
 */
#define isv6mcast(addr)	  ((addr)[0] == 0xff)
#define islinklocal(addr) ((addr)[0] == 0xfe && ((addr)[1] & 0xc0) == 0x80)

#define optexsts(np)	(nhgets((np)->ploadlen) > 24)
#define issmcast(addr)	(memcmp((addr), v6solicitednode, 13) == 0)

#ifndef MIN
#define MIN(a, b) ((a) <= (b)? (a): (b))
#endif

enum {				/* Header Types */
	HBH		= 0,	/* hop-by-hop multicast routing protocol */
	ICMP		= 1,
	IGMP		= 2,
	GGP		= 3,
	IPINIP		= 4,
	ST		= 5,
	TCP		= 6,
	UDP		= 17,
	ISO_TP4		= 29,
	RH		= 43,
	FH		= 44,
	IDRP		= 45,
	RSVP		= 46,
	AH		= 51,
	ESP		= 52,
	ICMPv6		= 58,
	NNH		= 59,
	DOH		= 60,
	ISO_IP		= 80,
	IGRP		= 88,
	OSPF		= 89,

	Maxhdrtype	= 256,
};

enum {
	/* multicast flags and scopes */

//	Well_known_flg	= 0,
//	Transient_flg	= 1,

//	Interface_local_scop = 1,
	Link_local_scop	= 2,
//	Site_local_scop	= 5,
//	Org_local_scop	= 8,
	Global_scop	= 14,

	/* various prefix lengths */
	SOLN_PREF_LEN	= 13,

	/* icmpv6 unreachability codes */
	Icmp6_no_route		= 0,
	Icmp6_ad_prohib		= 1,
	Icmp6_out_src_scope	= 2,
	Icmp6_adr_unreach	= 3,
	Icmp6_port_unreach	= 4,
	Icmp6_gress_src_fail	= 5,
	Icmp6_rej_route		= 6,
	Icmp6_unknown		= 7,  /* our own invention for internal use */

	/* various flags & constants */
	v6MINTU		= 1280,
	HOP_LIMIT	= 255,
	IP6HDR		= 40,		/* sizeof(Ip6hdr) = 8 + 2*16 */

	/* option types */

	/* neighbour discovery */
	SRC_LLADDR	= 1,
	TARGET_LLADDR	= 2,
	PREFIX_INFO	= 3,
	REDIR_HEADER	= 4,
	MTU_OPTION	= 5,
	/* new since rfc2461; see iana.org/assignments/icmpv6-parameters */
	V6nd_home	= 8,
	V6nd_srcaddrs	= 9,		/* rfc3122 */
	V6nd_ip		= 17,
	/* /lib/rfc/drafts/draft-jeong-dnsop-ipv6-dns-discovery-12.txt */
	V6nd_rdns	= 25,
	/* plan 9 extensions */
	V6nd_9fs	= 250,
	V6nd_9auth	= 251,

	SRC_UNSPEC	= 0,
	SRC_UNI		= 1,
	TARG_UNI	= 2,
	TARG_MULTI	= 3,

	Tunitent	= 1,
	Tuniproxy	= 2,
	Tunirany	= 3,

	/* Node constants */
	MAX_MULTICAST_SOLICIT	= 3,
	RETRANS_TIMER		= 1000,
};

/* we do this in case there's padding at the end of Ip6hdr */
#define IPV6HDR \
	uint8_t	vcf[4];		/* version:4, traffic class:8, flow label:20 */\
	uint8_t	ploadlen[2];	/* payload length: packet length - 40 */ \
	uint8_t	proto;		/* next header type */ \
	uint8_t	ttl;		/* hop limit */ \
	uint8_t	src[IPaddrlen]; \
	uint8_t	dst[IPaddrlen]

struct	ip6hdr {
	IPV6HDR;
	uint8_t	payload[];
};

struct	Opthdr {		/* unused */
	uint8_t	nexthdr;
	uint8_t	len;
};

/*
 * Beware routing header type 0 (loose source routing); see
 * http://www.secdev.org/conf/IPv6_RH_security-csw07.pdf.
 * Type 1 is unused.  Type 2 is for MIPv6 (mobile IPv6) filtering
 * against type 0 header.
 */
struct	Routinghdr {		/* unused */
	uint8_t	nexthdr;
	uint8_t	len;
	uint8_t	rtetype;
	uint8_t	segrem;
};

struct	fraghdr6 {
	uint8_t	nexthdr;
	uint8_t	res;
	uint8_t	offsetRM[2];	/* Offset, Res, M flag */
	uint8_t	id[4];
};

