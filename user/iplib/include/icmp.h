/* 
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */
/* ICMP for IP v4 and v6 */

enum
{
	/* Packet Types, icmp v4 (rfc 792) */
	EchoReply	= 0,
	Unreachable	= 3,
	SrcQuench	= 4,
	Redirect	= 5,
	EchoRequest	= 8,
	TimeExceed	= 11,
	InParmProblem	= 12,
	Timestamp	= 13,
	TimestampReply	= 14,
	InfoRequest	= 15,
	InfoReply	= 16,
	AddrMaskRequest = 17,
	AddrMaskReply   = 18,
	Traceroute	= 30,
	IPv6WhereAreYou	= 33,
	IPv6IAmHere	= 34,

	/* packet types, icmp v6 (rfc 2463) */

        /* error messages */
	UnreachableV6	= 1,
	PacketTooBigV6	= 2,
	TimeExceedV6	= 3,
	ParamProblemV6	= 4,

        /* informational messages (rfc 2461 also) */
	EchoRequestV6	= 128,
	EchoReplyV6	= 129,
	RouterSolicit	= 133,
	RouterAdvert	= 134,
	NbrSolicit	= 135,
	NbrAdvert	= 136,
	RedirectV6	= 137,

	Maxtype6	= 137,

	ICMP_HDRSIZE	= 8,
};

struct ip4hdr
{
	uint8_t	vihl;		/* Version and header length */
	uint8_t	tos;		/* Type of service */
	uint8_t	length[2];	/* packet length */
	uint8_t	id[2];		/* Identification */
	uint8_t	frag[2];	/* Fragment information */
	uint8_t	ttl;		/* Time to live */
	uint8_t	proto;		/* Protocol */
	uint8_t	ipcksum[2];	/* Header checksum */
	uint8_t	src[4];		/* Ipv4 source */
	uint8_t	dst[4];		/* Ipv4 destination */

	uint8_t	data[];
};

// #define IP4HDRSZ offsetof(Ip4hdr, data[0])

/* the icmp payload has the same format in v4 and v6 */

struct icmphdr {
	uint8_t	type;
	uint8_t	code;
	uint8_t	cksum[2];
	uint8_t	icmpid[2];
	uint8_t	seq[2];
	uint8_t	data[];
};

// #define ICMPHDRSZ offsetof(Icmphdr, data[0])
