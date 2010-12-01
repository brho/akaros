/*-
 * Copyright (c) 1982, 1986, 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)in.h	8.3 (Berkeley) 1/3/94
 * $FreeBSD$
 *
 * Dec 2010: Modified for ROS by Barret Rhoden <brho@cs.berkeley.edu>.  Took out
 * the structs we don't need yet, leaving the bits and some macros behind.
 */

#ifndef ROS_KERN_BITS_NETINET_H
#define ROS_KERN_BITS_NETINET_H

#include <ros/common.h>
#include <endian.h>

#define	IPPROTO_IP			0		/* dummy for IP */
#define	IPPROTO_ICMP		1		/* control message protocol */
#define	IPPROTO_TCP			6		/* tcp */
#define	IPPROTO_UDP			17		/* user datagram protocol */

#define	INADDR_ANY			(uint32_t)0x00000000
#define	INADDR_BROADCAST	(uint32_t)0xffffffff	/* must be masked */

#define	htonl(x) cpu_to_be32(x)
#define	htons(x) cpu_to_be16(x)
#define	ntohl(x) be32_to_cpu(x)
#define	ntohs(x) be16_to_cpu(x)

#define	IPPROTO_RAW			255		/* raw IP packet */
#define	INET_ADDRSTRLEN		16

/*
 * Constants and structures defined by the internet system,
 * Per RFC 790, September 1981, and numerous additions.
 */

/*
 * Protocols (RFC 1700)
 */
#define	IPPROTO_HOPOPTS		0		/* IP6 hop-by-hop options */
#define	IPPROTO_IGMP		2		/* group mgmt protocol */
#define	IPPROTO_GGP			3		/* gateway^2 (deprecated) */
#define	IPPROTO_IPV4		4		/* IPv4 encapsulation */
#define	IPPROTO_IPIP		IPPROTO_IPV4	/* for compatibility */
#define	IPPROTO_ST			7		/* Stream protocol II */
#define	IPPROTO_EGP			8		/* exterior gateway protocol */
#define	IPPROTO_PIGP		9		/* private interior gateway */
#define	IPPROTO_RCCMON		10		/* BBN RCC Monitoring */
#define	IPPROTO_NVPII		11		/* network voice protocol*/
#define	IPPROTO_PUP			12		/* pup */
#define	IPPROTO_ARGUS		13		/* Argus */
#define	IPPROTO_EMCON		14		/* EMCON */
#define	IPPROTO_XNET		15		/* Cross Net Debugger */
#define	IPPROTO_CHAOS		16		/* Chaos*/
#define	IPPROTO_MUX			18		/* Multiplexing */
#define	IPPROTO_MEAS		19		/* DCN Measurement Subsystems */
#define	IPPROTO_HMP			20		/* Host Monitoring */
#define	IPPROTO_PRM			21		/* Packet Radio Measurement */
#define	IPPROTO_IDP			22		/* xns idp */
#define	IPPROTO_TRUNK1		23		/* Trunk-1 */
#define	IPPROTO_TRUNK2		24		/* Trunk-2 */
#define	IPPROTO_LEAF1		25		/* Leaf-1 */
#define	IPPROTO_LEAF2		26		/* Leaf-2 */
#define	IPPROTO_RDP			27		/* Reliable Data */
#define	IPPROTO_IRTP		28		/* Reliable Transaction */
#define	IPPROTO_TP			29		/* tp-4 w/ class negotiation */
#define	IPPROTO_BLT			30		/* Bulk Data Transfer */
#define	IPPROTO_NSP			31		/* Network Services */
#define	IPPROTO_INP			32		/* Merit Internodal */
#define	IPPROTO_SEP			33		/* Sequential Exchange */
#define	IPPROTO_3PC			34		/* Third Party Connect */
#define	IPPROTO_IDPR		35		/* InterDomain Policy Routing */
#define	IPPROTO_XTP			36		/* XTP */
#define	IPPROTO_DDP			37		/* Datagram Delivery */
#define	IPPROTO_CMTP		38		/* Control Message Transport */
#define	IPPROTO_TPXX		39		/* TP++ Transport */
#define	IPPROTO_IL			40		/* IL transport protocol */
#define	IPPROTO_IPV6		41		/* IP6 header */
#define	IPPROTO_SDRP		42		/* Source Demand Routing */
#define	IPPROTO_ROUTING		43		/* IP6 routing header */
#define	IPPROTO_FRAGMENT	44		/* IP6 fragmentation header */
#define	IPPROTO_IDRP		45		/* InterDomain Routing*/
#define	IPPROTO_RSVP		46		/* resource reservation */
#define	IPPROTO_GRE			47		/* General Routing Encap. */
#define	IPPROTO_MHRP		48		/* Mobile Host Routing */
#define	IPPROTO_BHA			49		/* BHA */
#define	IPPROTO_ESP			50		/* IP6 Encap Sec. Payload */
#define	IPPROTO_AH			51		/* IP6 Auth Header */
#define	IPPROTO_INLSP		52		/* Integ. Net Layer Security */
#define	IPPROTO_SWIPE		53		/* IP with encryption */
#define	IPPROTO_NHRP		54		/* Next Hop Resolution */
#define	IPPROTO_MOBILE		55		/* IP Mobility */
#define	IPPROTO_TLSP		56		/* Transport Layer Security */
#define	IPPROTO_SKIP		57		/* SKIP */
#define	IPPROTO_ICMPV6		58		/* ICMP6 */
#define	IPPROTO_NONE		59		/* IP6 no next header */
#define	IPPROTO_DSTOPTS		60		/* IP6 destination option */
#define	IPPROTO_AHIP		61		/* any host internal protocol */
#define	IPPROTO_CFTP		62		/* CFTP */
#define	IPPROTO_HELLO		63		/* "hello" routing protocol */
#define	IPPROTO_SATEXPAK	64		/* SATNET/Backroom EXPAK */
#define	IPPROTO_KRYPTOLAN	65		/* Kryptolan */
#define	IPPROTO_RVD			66		/* Remote Virtual Disk */
#define	IPPROTO_IPPC		67		/* Pluribus Packet Core */
#define	IPPROTO_ADFS		68		/* Any distributed FS */
#define	IPPROTO_SATMON		69		/* Satnet Monitoring */
#define	IPPROTO_VISA		70		/* VISA Protocol */
#define	IPPROTO_IPCV		71		/* Packet Core Utility */
#define	IPPROTO_CPNX		72		/* Comp. Prot. Net. Executive */
#define	IPPROTO_CPHB		73		/* Comp. Prot. HeartBeat */
#define	IPPROTO_WSN			74		/* Wang Span Network */
#define	IPPROTO_PVP			75		/* Packet Video Protocol */
#define	IPPROTO_BRSATMON	76		/* BackRoom SATNET Monitoring */
#define	IPPROTO_ND			77		/* Sun net disk proto (temp.) */
#define	IPPROTO_WBMON		78		/* WIDEBAND Monitoring */
#define	IPPROTO_WBEXPAK		79		/* WIDEBAND EXPAK */
#define	IPPROTO_EON			80		/* ISO cnlp */
#define	IPPROTO_VMTP		81		/* VMTP */
#define	IPPROTO_SVMTP		82		/* Secure VMTP */
#define	IPPROTO_VINES		83		/* Banyon VINES */
#define	IPPROTO_TTP			84		/* TTP */
#define	IPPROTO_IGP			85		/* NSFNET-IGP */
#define	IPPROTO_DGP			86		/* dissimilar gateway prot. */
#define	IPPROTO_TCF			87		/* TCF */
#define	IPPROTO_IGRP		88		/* Cisco/GXS IGRP */
#define	IPPROTO_OSPFIGP		89		/* OSPFIGP */
#define	IPPROTO_SRPC		90		/* Strite RPC protocol */
#define	IPPROTO_LARP		91		/* Locus Address Resoloution */
#define	IPPROTO_MTP			92		/* Multicast Transport */
#define	IPPROTO_AX25		93		/* AX.25 Frames */
#define	IPPROTO_IPEIP		94		/* IP encapsulated in IP */
#define	IPPROTO_MICP		95		/* Mobile Int.ing control */
#define	IPPROTO_SCCSP		96		/* Semaphore Comm. security */
#define	IPPROTO_ETHERIP		97		/* Ethernet IP encapsulation */
#define	IPPROTO_ENCAP		98		/* encapsulation header */
#define	IPPROTO_APES		99		/* any private encr. scheme */
#define	IPPROTO_GMTP		100		/* GMTP*/
#define	IPPROTO_IPCOMP		108		/* payload compression (IPComp) */
#define	IPPROTO_SCTP		132		/* SCTP */
#define	IPPROTO_MH			135		/* IPv6 Mobility Header */
/* 101-254: Partly Unassigned */
#define	IPPROTO_PIM			103		/* Protocol Independent Mcast */
#define	IPPROTO_CARP		112		/* CARP */
#define	IPPROTO_PGM			113		/* PGM */
#define	IPPROTO_PFSYNC		240		/* PFSYNC */
/* 255: Reserved */

/*
 * Defined to avoid confusion.  The master value is defined by
 * PROTO_SPACER in sys/protosw.h.
 */
#define	IPPROTO_SPACER		32767		/* spacer for loadable protos */

/*
 * Local port number conventions:
 *
 * When a user does a bind(2) or connect(2) with a port number of zero,
 * a non-conflicting local port address is chosen.
 * The default range is IPPORT_HIFIRSTAUTO through
 * IPPORT_HILASTAUTO, although that is settable by sysctl.
 *
 * A user may set the IPPROTO_IP option IP_PORTRANGE to change this
 * default assignment range.
 *
 * The value IP_PORTRANGE_DEFAULT causes the default behavior.
 *
 * The value IP_PORTRANGE_HIGH changes the range of candidate port numbers
 * into the "high" range.  These are reserved for client outbound connections
 * which do not want to be filtered by any firewalls.
 *
 * The value IP_PORTRANGE_LOW changes the range to the "low" are
 * that is (by convention) restricted to privileged processes.  This
 * convention is based on "vouchsafe" principles only.  It is only secure
 * if you trust the remote host to restrict these ports.
 *
 * The default range of ports and the high range can be changed by
 * sysctl(3).  (net.inet.ip.port{hi,low}{first,last}_auto)
 *
 * Changing those values has bad security implications if you are
 * using a stateless firewall that is allowing packets outside of that
 * range in order to allow transparent outgoing connections.
 *
 * Such a firewall configuration will generally depend on the use of these
 * default values.  If you change them, you may find your Security
 * Administrator looking for you with a heavy object.
 *
 * For a slightly more orthodox text view on this:
 *
 *            ftp://ftp.isi.edu/in-notes/iana/assignments/port-numbers
 *
 *    port numbers are divided into three ranges:
 *
 *                0 -  1023 Well Known Ports
 *             1024 - 49151 Registered Ports
 *            49152 - 65535 Dynamic and/or Private Ports
 *
 */

/*
 * Ports < IPPORT_RESERVED are reserved for
 * privileged processes (e.g. root).         (IP_PORTRANGE_LOW)
 */
#define	IPPORT_RESERVED		1024

/*
 * Default local port range, used by IP_PORTRANGE_DEFAULT
 */
#define IPPORT_EPHEMERALFIRST	10000
#define IPPORT_EPHEMERALLAST	65535 
 
/*
 * Dynamic port range, used by IP_PORTRANGE_HIGH.
 */
#define	IPPORT_HIFIRSTAUTO	49152
#define	IPPORT_HILASTAUTO	65535

/*
 * Scanning for a free reserved port return a value below IPPORT_RESERVED,
 * but higher than IPPORT_RESERVEDSTART.  Traditionally the start value was
 * 512, but that conflicts with some well-known-services that firewalls may
 * have a fit if we use.
 */
#define	IPPORT_RESERVEDSTART	600

#define	IPPORT_MAX		65535

/*
 * Definitions of bits in internet address integers.
 * On subnets, the decomposition of addresses to host and net parts
 * is done according to subnet mask, not the masks here.
 */
#define	IN_CLASSA(i)		(((uint32_t)(i) & 0x80000000) == 0)
#define	IN_CLASSA_NET		0xff000000
#define	IN_CLASSA_NSHIFT	24
#define	IN_CLASSA_HOST		0x00ffffff
#define	IN_CLASSA_MAX		128

#define	IN_CLASSB(i)		(((uint32_t)(i) & 0xc0000000) == 0x80000000)
#define	IN_CLASSB_NET		0xffff0000
#define	IN_CLASSB_NSHIFT	16
#define	IN_CLASSB_HOST		0x0000ffff
#define	IN_CLASSB_MAX		65536

#define	IN_CLASSC(i)		(((uint32_t)(i) & 0xe0000000) == 0xc0000000)
#define	IN_CLASSC_NET		0xffffff00
#define	IN_CLASSC_NSHIFT	8
#define	IN_CLASSC_HOST		0x000000ff

#define	IN_CLASSD(i)		(((uint32_t)(i) & 0xf0000000) == 0xe0000000)
#define	IN_CLASSD_NET		0xf0000000	/* These ones aren't really */
#define	IN_CLASSD_NSHIFT	28		/* net and host fields, but */
#define	IN_CLASSD_HOST		0x0fffffff	/* routing needn't know.    */
#define	IN_MULTICAST(i)		IN_CLASSD(i)

#define	IN_EXPERIMENTAL(i)	(((uint32_t)(i) & 0xf0000000) == 0xf0000000)
#define	IN_BADCLASS(i)		(((uint32_t)(i) & 0xf0000000) == 0xf0000000)

#define IN_LINKLOCAL(i)		(((uint32_t)(i) & 0xffff0000) == 0xa9fe0000)
#define IN_LOOPBACK(i)		(((uint32_t)(i) & 0xff000000) == 0x7f000000)
#define IN_ZERONET(i)		(((uint32_t)(i) & 0xff000000) == 0)

#define	IN_PRIVATE(i)		((((uint32_t)(i) & 0xff000000) == 0x0a000000) || \
							 (((uint32_t)(i) & 0xfff00000) == 0xac100000) || \
							 (((uint32_t)(i) & 0xffff0000) == 0xc0a80000))

#define	IN_LOCAL_GROUP(i)	(((uint32_t)(i) & 0xffffff00) == 0xe0000000)
 
#define	IN_ANY_LOCAL(i)		(IN_LINKLOCAL(i) || IN_LOCAL_GROUP(i))

#define	INADDR_LOOPBACK			(uint32_t)0x7f000001

#define	INADDR_UNSPEC_GROUP		(uint32_t)0xe0000000	/* 224.0.0.0 */
#define	INADDR_ALLHOSTS_GROUP	(uint32_t)0xe0000001	/* 224.0.0.1 */
#define	INADDR_ALLRTRS_GROUP	(uint32_t)0xe0000002	/* 224.0.0.2 */
#define	INADDR_ALLRPTS_GROUP	(uint32_t)0xe0000016	/* 224.0.0.22, IGMPv3 */
#define	INADDR_CARP_GROUP		(uint32_t)0xe0000012	/* 224.0.0.18 */
#define	INADDR_PFSYNC_GROUP		(uint32_t)0xe00000f0	/* 224.0.0.240 */
#define	INADDR_ALLMDNS_GROUP	(uint32_t)0xe00000fb	/* 224.0.0.251 */
#define	INADDR_MAX_LOCAL_GROUP	(uint32_t)0xe00000ff	/* 224.0.0.255 */

#define	IN_LOOPBACKNET		127			/* official! */

/*
 * Options for use with [gs]etsockopt at the IP level.
 * First word of comment is data type; bool is stored in int.
 */
#define	IP_OPTIONS				1    /* buf/ip_opts; set/get IP options */
#define	IP_HDRINCL				2    /* int; header is included with data */
#define	IP_TOS					3    /* int; IP type of service and preced. */
#define	IP_TTL					4    /* int; IP time to live */
#define	IP_RECVOPTS				5    /* bool; receive all IP opts w/dgram */
#define	IP_RECVRETOPTS			6    /* bool; receive IP opts for response */
#define	IP_RECVDSTADDR			7    /* bool; receive IP dst addr w/dgram */
#define	IP_SENDSRCADDR			IP_RECVDSTADDR /* cmsg_type to set src addr */
#define	IP_RETOPTS				8    /* ip_opts; set/get IP options */
#define	IP_MULTICAST_IF			9    /* struct in_addr *or* struct ip_mreqn;
				                      * set/get IP multicast i/f  */
#define	IP_MULTICAST_TTL		10   /* u_char; set/get IP multicast ttl */
#define	IP_MULTICAST_LOOP		11   /* u_char; set/get IP multicast loopback */
#define	IP_ADD_MEMBERSHIP		12   /* ip_mreq; add an IP group membership */
#define	IP_DROP_MEMBERSHIP		13   /* ip_mreq; drop an IP group membership */
#define	IP_MULTICAST_VIF		14   /* set/get IP mcast virt. iface */
#define	IP_RSVP_ON				15   /* enable RSVP in kernel */
#define	IP_RSVP_OFF				16   /* disable RSVP in kernel */
#define	IP_RSVP_VIF_ON			17   /* set RSVP per-vif socket */
#define	IP_RSVP_VIF_OFF			18   /* unset RSVP per-vif socket */
#define	IP_PORTRANGE			19   /* int; range to choose for unspec port */
#define	IP_RECVIF				20   /* bool; receive reception if w/dgram */
/* for IPSEC */
#define	IP_IPSEC_POLICY			21   /* int; set/get security policy */
#define	IP_FAITH				22   /* bool; accept FAITH'ed connections */

#define	IP_ONESBCAST			23   /* bool: send all-ones broadcast */
#define	IP_BINDANY				24   /* bool: allow bind to any address */

/*
 * Options for controlling the firewall and dummynet.
 * Historical options (from 40 to 64) will eventually be
 * replaced by only two options, IP_FW3 and IP_DUMMYNET3.
 */
#define	IP_FW_TABLE_ADD			40   /* add entry */
#define	IP_FW_TABLE_DEL			41   /* delete entry */
#define	IP_FW_TABLE_FLUSH		42   /* flush table */
#define	IP_FW_TABLE_GETSIZE		43   /* get table size */
#define	IP_FW_TABLE_LIST		44   /* list table contents */

#define	IP_FW3					48   /* generic ipfw v.3 sockopts */
#define	IP_DUMMYNET3			49   /* generic dummynet v.3 sockopts */

#define	IP_FW_ADD				50   /* add a firewall rule to chain */
#define	IP_FW_DEL				51   /* delete a firewall rule from chain */
#define	IP_FW_FLUSH				52   /* flush firewall rule chain */
#define	IP_FW_ZERO				53   /* clear single/all firewall counter(s) */
#define	IP_FW_GET				54   /* get entire firewall rule chain */
#define	IP_FW_RESETLOG			55   /* reset logging counters */

#define IP_FW_NAT_CFG           56   /* add/config a nat rule */
#define IP_FW_NAT_DEL           57   /* delete a nat rule */
#define IP_FW_NAT_GET_CONFIG    58   /* get configuration of a nat rule */
#define IP_FW_NAT_GET_LOG       59   /* get log of a nat rule */

#define	IP_DUMMYNET_CONFIGURE	60   /* add/configure a dummynet pipe */
#define	IP_DUMMYNET_DEL			61   /* delete a dummynet pipe from chain */
#define	IP_DUMMYNET_FLUSH		62   /* flush dummynet */
#define	IP_DUMMYNET_GET			64   /* get entire dummynet pipes */

#define	IP_RECVTTL				65   /* bool; receive IP TTL w/dgram */
#define	IP_MINTTL				66   /* minimum TTL for packet or drop */
#define	IP_DONTFRAG				67   /* don't fragment packet */

/* IPv4 Source Filter Multicast API [RFC3678] */
#define	IP_ADD_SOURCE_MEMBERSHIP	70   /* join a source-specific group */
#define	IP_DROP_SOURCE_MEMBERSHIP	71   /* drop a single source */
#define	IP_BLOCK_SOURCE				72   /* block a source */
#define	IP_UNBLOCK_SOURCE			73   /* unblock a source */

/* The following option is private; do not use it from user applications. */
#define	IP_MSFILTER					74   /* set/get filter list */

/* Protocol Independent Multicast API [RFC3678] */
#define	MCAST_JOIN_GROUP			80   /* join an any-source group */
#define	MCAST_LEAVE_GROUP			81   /* leave all sources for group */
#define	MCAST_JOIN_SOURCE_GROUP		82   /* join a source-specific group */
#define	MCAST_LEAVE_SOURCE_GROUP	83   /* leave a single source */
#define	MCAST_BLOCK_SOURCE			84   /* block a source */
#define	MCAST_UNBLOCK_SOURCE		85   /* unblock a source */

/*
 * Defaults and limits for options
 */
#define	IP_DEFAULT_MULTICAST_TTL  1	/* normally limit m'casts to 1 hop  */
#define	IP_DEFAULT_MULTICAST_LOOP 1	/* normally hear sends if a member  */

/*
 * The imo_membership vector for each socket is now dynamically allocated at
 * run-time, bounded by USHRT_MAX, and is reallocated when needed, sized
 * according to a power-of-two increment.
 */
#define	IP_MIN_MEMBERSHIPS	31
#define	IP_MAX_MEMBERSHIPS	4095

/*
 * Default resource limits for IPv4 multicast source filtering.
 * These may be modified by sysctl.
 */
#define	IP_MAX_GROUP_SRC_FILTER		512	/* sources per group */
#define	IP_MAX_SOCK_SRC_FILTER		128	/* sources per socket/group */

#endif /* ROS_KERN_BITS_NETINET_H */
