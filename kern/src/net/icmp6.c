/* Copyright © 1994-1999 Lucent Technologies Inc.  All rights reserved.
 * Portions Copyright © 1997-1999 Vita Nuova Limited
 * Portions Copyright © 2000-2007 Vita Nuova Holdings Limited
 *                                (www.vitanuova.com)
 * Revisions Copyright © 2000-2007 Lucent Technologies Inc. and others
 *
 * Modified for the Akaros operating system:
 * Copyright (c) 2013-2014 The Regents of the University of California
 * Copyright (c) 2013-2015 Google Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE. */

#include <kmalloc.h>
#include <string.h>
#include <stdio.h>
#include <syscall.h>
#include <error.h>
#include <ip.h>

struct ICMPpkt {
	uint8_t type;
	uint8_t code;
	uint8_t cksum[2];
	uint8_t icmpid[2];
	uint8_t seq[2];
};

/* plan 9 uses anon struct members.
 * We have been naming the struct
 * members and just using the extra level of deref
 * e.g. i->x becomes i->i6->x.
 */
struct IPICMP {
/*
	Ip6hdr;
	ICMPpkt;
*/
	uint8_t vcf[4];				// version:4, traffic class:8, flow label:20
	uint8_t ploadlen[2];		// payload length: packet length - 40
	uint8_t proto;				// next header type
	uint8_t ttl;				// hop limit
	uint8_t src[IPaddrlen];
	uint8_t dst[IPaddrlen];
	uint8_t type;
	uint8_t code;
	uint8_t cksum[2];
	uint8_t icmpid[2];
	uint8_t seq[2];

};

struct NdiscC {
	//IPICMP;
	uint8_t vcf[4];				// version:4, traffic class:8, flow label:20
	uint8_t ploadlen[2];		// payload length: packet length - 40
	uint8_t proto;				// next header type
	uint8_t ttl;				// hop limit
	uint8_t src[IPaddrlen];
	uint8_t dst[IPaddrlen];
	uint8_t type;
	uint8_t code;
	uint8_t cksum[2];
	uint8_t icmpid[2];
	uint8_t seq[2];

	uint8_t target[IPaddrlen];
};

struct Ndpkt {
	//NdiscC;
	uint8_t vcf[4];				// version:4, traffic class:8, flow label:20
	uint8_t ploadlen[2];		// payload length: packet length - 40
	uint8_t proto;				// next header type
	uint8_t ttl;				// hop limit
	uint8_t src[IPaddrlen];
	uint8_t dst[IPaddrlen];
	uint8_t type;
	uint8_t code;
	uint8_t cksum[2];
	uint8_t icmpid[2];
	uint8_t seq[2];

	uint8_t target[IPaddrlen];

	uint8_t otype;
	uint8_t olen;				// length in units of 8 octets(incl type, code),
	// 1 for IEEE 802 addresses
	uint8_t lnaddr[6];			// link-layer address
};

enum {
	// ICMPv6 types
	EchoReply = 0,
	UnreachableV6 = 1,
	PacketTooBigV6 = 2,
	TimeExceedV6 = 3,
	SrcQuench = 4,
	ParamProblemV6 = 4,
	Redirect = 5,
	EchoRequest = 8,
	TimeExceed = 11,
	InParmProblem = 12,
	Timestamp = 13,
	TimestampReply = 14,
	InfoRequest = 15,
	InfoReply = 16,
	AddrMaskRequest = 17,
	AddrMaskReply = 18,
	EchoRequestV6 = 128,
	EchoReplyV6 = 129,
	RouterSolicit = 133,
	RouterAdvert = 134,
	NbrSolicit = 135,
	NbrAdvert = 136,
	RedirectV6 = 137,

	Maxtype6 = 137,
};

char *icmpnames6[Maxtype6 + 1] = {
	[EchoReply] "EchoReply",
	[UnreachableV6] "UnreachableV6",
	[PacketTooBigV6] "PacketTooBigV6",
	[TimeExceedV6] "TimeExceedV6",
	[SrcQuench] "SrcQuench",
	[Redirect] "Redirect",
	[EchoRequest] "EchoRequest",
	[TimeExceed] "TimeExceed",
	[InParmProblem] "InParmProblem",
	[Timestamp] "Timestamp",
	[TimestampReply] "TimestampReply",
	[InfoRequest] "InfoRequest",
	[InfoReply] "InfoReply",
	[AddrMaskRequest] "AddrMaskRequest",
	[AddrMaskReply] "AddrMaskReply",
	[EchoRequestV6] "EchoRequestV6",
	[EchoReplyV6] "EchoReplyV6",
	[RouterSolicit] "RouterSolicit",
	[RouterAdvert] "RouterAdvert",
	[NbrSolicit] "NbrSolicit",
	[NbrAdvert] "NbrAdvert",
	[RedirectV6] "RedirectV6",
};

enum {
	InMsgs6,
	InErrors6,
	OutMsgs6,
	CsumErrs6,
	LenErrs6,
	HlenErrs6,
	HoplimErrs6,
	IcmpCodeErrs6,
	TargetErrs6,
	OptlenErrs6,
	AddrmxpErrs6,
	RouterAddrErrs6,

	Nstats6,
};

static char *statnames6[Nstats6] = {
	[InMsgs6] "InMsgs",
	[InErrors6] "InErrors",
	[OutMsgs6] "OutMsgs",
	[CsumErrs6] "CsumErrs",
	[LenErrs6] "LenErrs",
	[HlenErrs6] "HlenErrs",
	[HoplimErrs6] "HoplimErrs",
	[IcmpCodeErrs6] "IcmpCodeErrs",
	[TargetErrs6] "TargetErrs",
	[OptlenErrs6] "OptlenErrs",
	[AddrmxpErrs6] "AddrmxpErrs",
	[RouterAddrErrs6] "RouterAddrErrs",
};

typedef struct Icmppriv6 {
	uint32_t stats[Nstats6];

	/* message counts */
	uint32_t in[Maxtype6 + 1];
	uint32_t out[Maxtype6 + 1];
} Icmppriv6;

typedef struct Icmpcb6 {
	uint8_t headers;
} Icmpcb6;

static char *unreachcode[] = {
	[icmp6_no_route] "no route to destination",
	[icmp6_ad_prohib] "comm with destination administratively prohibited",
	[icmp6_unassigned] "icmp unreachable: unassigned error code (2)",
	[icmp6_adr_unreach] "address unreachable",
	[icmp6_port_unreach] "port unreachable",
	[icmp6_unkn_code] "icmp unreachable: unknown code",
};

enum {
	ICMP_USEAD6 = 40,
};

enum {
	Oflag = 1 << 5,
	Sflag = 1 << 6,
	Rflag = 1 << 7,
};

enum {
	slladd = 1,
	tlladd = 2,
	prfinfo = 3,
	redhdr = 4,
	mtuopt = 5,
};

static void icmpkick6(void *x, struct block *bp);

static void icmpcreate6(struct conv *c)
{
	c->rq = qopen(64 * 1024, Qmsg, 0, c);
	c->wq = qbypass(icmpkick6, c);
}

static void set_cksum(struct block *bp)
{
	struct IPICMP *p = (struct IPICMP *)(bp->rp);

	hnputl(p->vcf, 0);	// borrow IP header as pseudoheader
	hnputs(p->ploadlen, blocklen(bp) - IPV6HDR_LEN);
	p->proto = 0;
	p->ttl = ICMPv6;	// ttl gets set later
	hnputs(p->cksum, 0);
	hnputs(p->cksum, ptclcsum(bp, 0, blocklen(bp)));
	p->proto = ICMPv6;
}

static struct block *newIPICMP(int packetlen)
{
	struct block *nbp;
	nbp = block_alloc(packetlen, MEM_WAIT);
	nbp->wp += packetlen;
	memset(nbp->rp, 0, packetlen);
	return nbp;
}

void icmpadvise6(struct Proto *icmp, struct block *bp, char *msg)
{
	struct conv **c, *s;
	struct IPICMP *p;
	uint16_t recid;

	p = (struct IPICMP *)bp->rp;
	recid = nhgets(p->icmpid);

	for (c = icmp->conv; *c; c++) {
		s = *c;
		if (s->lport == recid)
			if (ipcmp(s->raddr, p->dst) == 0) {
				qhangup(s->rq, msg);
				qhangup(s->wq, msg);
				break;
			}
	}
	freeblist(bp);
}

static void icmpkick6(void *x, struct block *bp)
{
	struct conv *c = x;
	struct IPICMP *p;
	uint8_t laddr[IPaddrlen], raddr[IPaddrlen];
	Icmppriv6 *ipriv = c->p->priv;
	Icmpcb6 *icb = (struct Icmpcb6 *)c->ptcl;

	if (bp == NULL)
		return;

	if (icb->headers == 6) {
		/* get user specified addresses */
		bp = pullupblock(bp, ICMP_USEAD6);
		if (bp == NULL)
			return;
		bp->rp += 8;
		ipmove(laddr, bp->rp);
		bp->rp += IPaddrlen;
		ipmove(raddr, bp->rp);
		bp->rp += IPaddrlen;
		bp = padblock(bp, sizeof(struct ip6hdr));

		if (blocklen(bp) < sizeof(struct IPICMP)) {
			freeblist(bp);
			return;
		}
		p = (struct IPICMP *)(bp->rp);

		ipmove(p->dst, raddr);
		ipmove(p->src, laddr);

	} else {
		if (blocklen(bp) < sizeof(struct IPICMP)) {
			freeblist(bp);
			return;
		}
		p = (struct IPICMP *)(bp->rp);

		ipmove(p->dst, c->raddr);
		ipmove(p->src, c->laddr);
		hnputs(p->icmpid, c->lport);
	}

	set_cksum(bp);
	p->vcf[0] = 0x06 << 4;
	if (p->type <= Maxtype6)
		ipriv->out[p->type]++;
	ipoput6(c->p->f, bp, 0, c->ttl, c->tos, NULL);
}

static void icmpctl6(struct conv *c, char **argv, int argc)
{
	Icmpcb6 *icb = (Icmpcb6*)c->ptcl;

	if ((argc == 1) && strcmp(argv[0], "headers") == 0)
		icb->headers = 6;
	else
		error(EINVAL, "unknown command to icmpctl6");
}

static void goticmpkt6(struct Proto *icmp, struct block *bp, int muxkey)
{
	struct conv **c, *s;
	struct IPICMP *p = (struct IPICMP *)bp->rp;
	uint16_t recid;
	uint8_t *addr;

	if (muxkey == 0) {
		recid = nhgets(p->icmpid);
		addr = p->src;
	} else {
		recid = muxkey;
		addr = p->dst;
	}

	for (c = icmp->conv; *c; c++) {
		s = *c;
		if (s->lport == recid && ipcmp(s->raddr, addr) == 0) {
			bp = concatblock(bp);
			if (bp != NULL)
				qpass(s->rq, bp);
			return;
		}
	}

	freeblist(bp);
}

static struct block *mkechoreply6(struct block *bp)
{
	struct IPICMP *p = (struct IPICMP *)(bp->rp);
	uint8_t addr[IPaddrlen];

	ipmove(addr, p->src);
	ipmove(p->src, p->dst);
	ipmove(p->dst, addr);
	p->type = EchoReplyV6;
	set_cksum(bp);
	return bp;
}

/*
 * sends out an ICMPv6 neighbor solicitation
 * 	suni == SRC_UNSPEC or SRC_UNI,
 *	tuni == TARG_MULTI => multicast for address resolution,
 * 	and tuni == TARG_UNI => neighbor reachability.
 */

void icmpns(struct Fs *f,
            uint8_t *src, int suni,
            uint8_t *targ, int tuni,
            uint8_t *mac)
{
	struct block *nbp;
	struct Ndpkt *np;
	struct Proto *icmp = f->t2p[ICMPv6];
	struct Icmppriv6 *ipriv = icmp->priv;

	nbp = newIPICMP(sizeof(struct Ndpkt));
	np = (struct Ndpkt *)nbp->rp;

	if (suni == SRC_UNSPEC)
		memmove(np->src, v6Unspecified, IPaddrlen);
	else
		memmove(np->src, src, IPaddrlen);

	if (tuni == TARG_UNI)
		memmove(np->dst, targ, IPaddrlen);
	else
		ipv62smcast(np->dst, targ);

	np->type = NbrSolicit;
	np->code = 0;
	memmove(np->target, targ, IPaddrlen);
	if (suni != SRC_UNSPEC) {
		np->otype = SRC_LLADDRESS;
		np->olen = 1;	/* 1+1+6 = 8 = 1 8-octet */
		memmove(np->lnaddr, mac, sizeof(np->lnaddr));
	} else {
		int r = sizeof(struct Ndpkt) - sizeof(struct NdiscC);
		nbp->wp -= r;
	}

	set_cksum(nbp);
	np = (struct Ndpkt *)nbp->rp;
	np->ttl = HOP_LIMIT;
	np->vcf[0] = 0x06 << 4;
	ipriv->out[NbrSolicit]++;
	netlog(f, Logicmp, "sending neighbor solicitation %I\n", targ);
	ipoput6(f, nbp, 0, MAXTTL, DFLTTOS, NULL);
}

/*
 * sends out an ICMPv6 neighbor advertisement. pktflags == RSO flags.
 */
void icmpna(struct Fs *f, uint8_t * src, uint8_t * dst, uint8_t * targ,
            uint8_t * mac, uint8_t flags)
{
	struct block *nbp;
	struct Ndpkt *np;
	struct Proto *icmp = f->t2p[ICMPv6];
	Icmppriv6 *ipriv = icmp->priv;

	nbp = newIPICMP(sizeof(struct Ndpkt));
	np = (struct Ndpkt *)nbp->rp;

	memmove(np->src, src, IPaddrlen);
	memmove(np->dst, dst, IPaddrlen);

	np->type = NbrAdvert;
	np->code = 0;
	np->icmpid[0] = flags;
	memmove(np->target, targ, IPaddrlen);

	np->otype = TARGET_LLADDRESS;
	np->olen = 1;
	memmove(np->lnaddr, mac, sizeof(np->lnaddr));

	set_cksum(nbp);
	np = (struct Ndpkt *)nbp->rp;
	np->ttl = HOP_LIMIT;
	np->vcf[0] = 0x06 << 4;
	ipriv->out[NbrAdvert]++;
	netlog(f, Logicmp, "sending neighbor advertisement %I\n", src);
	ipoput6(f, nbp, 0, MAXTTL, DFLTTOS, NULL);
}

void icmphostunr(struct Fs *f, struct Ipifc *ifc,
                 struct block *bp, int code, int free)
{
	struct block *nbp;
	struct IPICMP *np;
	struct ip6hdr *p;
	int osz = BLEN(bp);
	int sz = MIN(sizeof(struct IPICMP) + osz, v6MINTU);
	struct Proto *icmp = f->t2p[ICMPv6];
	Icmppriv6 *ipriv = icmp->priv;

	p = (struct ip6hdr *)bp->rp;

	if (isv6mcast(p->src))
		goto freebl;

	nbp = newIPICMP(sz);
	np = (struct IPICMP *)nbp->rp;

	rlock(&ifc->rwlock);
	if (ipv6anylocal(ifc, np->src)) {
		netlog(f, Logicmp, "send icmphostunr -> s%I d%I\n", p->src, p->dst);
	} else {
		netlog(f, Logicmp, "icmphostunr fail -> s%I d%I\n", p->src, p->dst);
		runlock(&ifc->rwlock);
		freeblist(nbp);
		goto freebl;
	}

	memmove(np->dst, p->src, IPaddrlen);
	np->type = UnreachableV6;
	np->code = code;
	memmove(nbp->rp + sizeof(struct IPICMP), bp->rp,
			sz - sizeof(struct IPICMP));
	set_cksum(nbp);
	np->ttl = HOP_LIMIT;
	np->vcf[0] = 0x06 << 4;
	ipriv->out[UnreachableV6]++;

	if (free)
		ipiput6(f, ifc, nbp);
	else
		ipoput6(f, nbp, 0, MAXTTL, DFLTTOS, NULL);
	runlock(&ifc->rwlock);
freebl:
	if (free)
		freeblist(bp);
}

void icmpttlexceeded6(struct Fs *f, struct Ipifc *ifc, struct block *bp)
{
	struct block *nbp;
	struct IPICMP *np;
	struct ip6hdr *p;
	int osz = BLEN(bp);
	int sz = MIN(sizeof(struct IPICMP) + osz, v6MINTU);
	struct Proto *icmp = f->t2p[ICMPv6];
	Icmppriv6 *ipriv = icmp->priv;

	p = (struct ip6hdr *)bp->rp;

	if (isv6mcast(p->src))
		return;

	nbp = newIPICMP(sz);
	np = (struct IPICMP *)nbp->rp;

	if (ipv6anylocal(ifc, np->src)) {
		netlog(f, Logicmp, "send icmpttlexceeded6 -> s%I d%I\n", p->src,
		       p->dst);
	} else {
		netlog(f, Logicmp, "icmpttlexceeded6 fail -> s%I d%I\n", p->src,
		       p->dst);
		return;
	}

	memmove(np->dst, p->src, IPaddrlen);
	np->type = TimeExceedV6;
	np->code = 0;
	memmove(nbp->rp + sizeof(struct IPICMP), bp->rp,
	        sz - sizeof(struct IPICMP));
	set_cksum(nbp);
	np->ttl = HOP_LIMIT;
	np->vcf[0] = 0x06 << 4;
	ipriv->out[TimeExceedV6]++;
	ipoput6(f, nbp, 0, MAXTTL, DFLTTOS, NULL);
}

void icmppkttoobig6(struct Fs *f, struct Ipifc *ifc, struct block *bp)
{
	struct block *nbp;
	struct IPICMP *np;
	struct ip6hdr *p;
	int osz = BLEN(bp);
	int sz = MIN(sizeof(struct IPICMP) + osz, v6MINTU);
	struct Proto *icmp = f->t2p[ICMPv6];
	Icmppriv6 *ipriv = icmp->priv;

	p = (struct ip6hdr *)bp->rp;

	if (isv6mcast(p->src))
		return;

	nbp = newIPICMP(sz);
	np = (struct IPICMP *)nbp->rp;

	if (!ipv6anylocal(ifc, np->src)) {
		netlog(f, Logicmp, "icmppkttoobig6 fail -> s%I d%I\n", p->src, p->dst);
		return;
	}
	netlog(f, Logicmp, "send icmppkttoobig6 -> s%I d%I\n", p->src, p->dst);

	memmove(np->dst, p->src, IPaddrlen);
	np->type = PacketTooBigV6;
	np->code = 0;
	hnputl(np->icmpid, ifc->maxtu - ifc->m->hsize);
	memmove(nbp->rp + sizeof(struct IPICMP), bp->rp,
			sz - sizeof(struct IPICMP));
	set_cksum(nbp);
	np->ttl = HOP_LIMIT;
	np->vcf[0] = 0x06 << 4;
	ipriv->out[PacketTooBigV6]++;
	ipoput6(f, nbp, 0, MAXTTL, DFLTTOS, NULL);
}

/*
 * RFC 2461, pages 39-40, pages 57-58.
 */
static int valid(struct Proto *icmp, struct Ipifc *ifc,
                 struct block *bp, Icmppriv6 * ipriv)
{
	int sz, osz, unsp, n, ttl, iplen;
	int pktsz = BLEN(bp);
	uint8_t *packet = bp->rp;
	struct IPICMP *p = (struct IPICMP *)packet;
	struct Ndpkt *np;

	n = blocklen(bp);
	if (n < sizeof(struct IPICMP)) {
		ipriv->stats[HlenErrs6]++;
		netlog(icmp->f, Logicmp, "icmp hlen %d\n", n);
		goto err;
	}

	iplen = nhgets(p->ploadlen);
	if (iplen > n - IPV6HDR_LEN || (iplen % 1)) {
		ipriv->stats[LenErrs6]++;
		netlog(icmp->f, Logicmp, "icmp length %d\n", iplen);
		goto err;
	}
	// Rather than construct explicit pseudoheader, overwrite IPv6 header
	if (p->proto != ICMPv6) {
		// This code assumes no extension headers!!!
		netlog(icmp->f, Logicmp, "icmp error: extension header\n");
		goto err;
	}
	memset(packet, 0, 4);
	ttl = p->ttl;
	p->ttl = p->proto;
	p->proto = 0;
	if (ptclcsum(bp, 0, iplen + IPV6HDR_LEN)) {
		ipriv->stats[CsumErrs6]++;
		netlog(icmp->f, Logicmp, "icmp checksum error\n");
		goto err;
	}
	p->proto = p->ttl;
	p->ttl = ttl;

	/* additional tests for some pkt types */
	if ((p->type == NbrSolicit) ||
		(p->type == NbrAdvert) ||
		(p->type == RouterAdvert) ||
		(p->type == RouterSolicit) || (p->type == RedirectV6)) {

		if (p->ttl != HOP_LIMIT) {
			ipriv->stats[HoplimErrs6]++;
			goto err;
		}
		if (p->code != 0) {
			ipriv->stats[IcmpCodeErrs6]++;
			goto err;
		}

		switch (p->type) {
			case NbrSolicit:
			case NbrAdvert:
				np = (struct Ndpkt *)p;
				if (isv6mcast(np->target)) {
					ipriv->stats[TargetErrs6]++;
					goto err;
				}
				if (optexsts(np) && (np->olen == 0)) {
					ipriv->stats[OptlenErrs6]++;
					goto err;
				}

				if (p->type == NbrSolicit) {
					if (ipcmp(np->src, v6Unspecified) == 0) {
						if (!issmcast(np->dst) || optexsts(np)) {
							ipriv->stats[AddrmxpErrs6]++;
							goto err;
						}
					}
				}

				if (p->type == NbrAdvert) {
					if ((isv6mcast(np->dst)) && (nhgets(np->icmpid) & Sflag)) {
						ipriv->stats[AddrmxpErrs6]++;
						goto err;
					}
				}
				break;

			case RouterAdvert:
				if (pktsz - sizeof(struct ip6hdr) < 16) {
					ipriv->stats[HlenErrs6]++;
					goto err;
				}
				if (!islinklocal(p->src)) {
					ipriv->stats[RouterAddrErrs6]++;
					goto err;
				}
				sz = sizeof(struct IPICMP) + 8;
				while ((sz + 1) < pktsz) {
					osz = *(packet + sz + 1);
					if (osz <= 0) {
						ipriv->stats[OptlenErrs6]++;
						goto err;
					}
					sz += 8 * osz;
				}
				break;

			case RouterSolicit:
				if (pktsz - sizeof(struct ip6hdr) < 8) {
					ipriv->stats[HlenErrs6]++;
					goto err;
				}
				unsp = (ipcmp(p->src, v6Unspecified) == 0);
				sz = sizeof(struct IPICMP) + 8;
				while ((sz + 1) < pktsz) {
					osz = *(packet + sz + 1);
					if ((osz <= 0) || (unsp && (*(packet + sz) == slladd))) {
						ipriv->stats[OptlenErrs6]++;
						goto err;
					}
					sz += 8 * osz;
				}
				break;

			case RedirectV6:
				//to be filled in
				break;

			default:
				goto err;
		}
	}

	return 1;

err:
	ipriv->stats[InErrors6]++;
	return 0;
}

static int targettype(struct Fs *f, struct Ipifc *ifc, uint8_t * target)
{
	struct Iplifc *lifc;
	int t;

	rlock(&ifc->rwlock);
	if (ipproxyifc(f, ifc, target)) {
		runlock(&ifc->rwlock);
		return t_uniproxy;
	}

	for (lifc = ifc->lifc; lifc; lifc = lifc->next) {
		if (ipcmp(lifc->local, target) == 0) {
			t = (lifc->tentative) ? t_unitent : t_unirany;
			runlock(&ifc->rwlock);
			return t;
		}
	}

	runlock(&ifc->rwlock);
	return 0;
}

static void icmpiput6(struct Proto *icmp, struct Ipifc *ipifc, struct block *bp)
{
	uint8_t *packet = bp->rp;
	struct IPICMP *p = (struct IPICMP *)packet;
	Icmppriv6 *ipriv = icmp->priv;
	struct block *r;
	struct Proto *pr;
	char *msg, m2[128];
	struct Ndpkt *np;
	uint8_t pktflags;
	uint8_t lsrc[IPaddrlen];
	int refresh = 1;
	struct Iplifc *lifc;

	if (!valid(icmp, ipifc, bp, ipriv))
		goto raise;

	if (p->type <= Maxtype6)
		ipriv->in[p->type]++;
	else
		goto raise;

	switch (p->type) {
		case EchoRequestV6:
			r = mkechoreply6(bp);
			ipriv->out[EchoReply]++;
			ipoput6(icmp->f, r, 0, MAXTTL, DFLTTOS, NULL);
			break;

		case UnreachableV6:
			if (p->code > 4)
				msg = unreachcode[icmp6_unkn_code];
			else
				msg = unreachcode[p->code];

			bp->rp += sizeof(struct IPICMP);
			if (blocklen(bp) < 8) {
				ipriv->stats[LenErrs6]++;
				goto raise;
			}
			p = (struct IPICMP *)bp->rp;
			pr = Fsrcvpcolx(icmp->f, p->proto);
			if (pr != NULL && pr->advise != NULL) {
				(*pr->advise) (pr, bp, msg);
				return;
			}

			bp->rp -= sizeof(struct IPICMP);
			goticmpkt6(icmp, bp, 0);
			break;

		case TimeExceedV6:
			if (p->code == 0) {
				snprintf(m2, sizeof(m2), "ttl exceeded at %I", p->src);

				bp->rp += sizeof(struct IPICMP);
				if (blocklen(bp) < 8) {
					ipriv->stats[LenErrs6]++;
					goto raise;
				}
				p = (struct IPICMP *)bp->rp;
				pr = Fsrcvpcolx(icmp->f, p->proto);
				if (pr != NULL && pr->advise != NULL) {
					(*pr->advise) (pr, bp, m2);
					return;
				}
				bp->rp -= sizeof(struct IPICMP);
			}

			goticmpkt6(icmp, bp, 0);
			break;

		case RouterAdvert:
		case RouterSolicit:
			/* using lsrc as a temp, munge hdr for goticmp6
			   memmove(lsrc, p->src, IPaddrlen);
			   memmove(p->src, p->dst, IPaddrlen);
			   memmove(p->dst, lsrc, IPaddrlen); */

			goticmpkt6(icmp, bp, p->type);
			break;

		case NbrSolicit:
			np = (struct Ndpkt *)p;
			pktflags = 0;
			switch (targettype(icmp->f, ipifc, np->target)) {
				case t_unirany:
					pktflags |= Oflag;
					/* fall through */

				case t_uniproxy:
					if (ipcmp(np->src, v6Unspecified) != 0) {
						arpenter(icmp->f, V6, np->src, np->lnaddr,
								 8 * np->olen - 2, 0);
						pktflags |= Sflag;
					}
					if (ipv6local(ipifc, lsrc)) {
						icmpna(icmp->f, lsrc,
							   (ipcmp(np->src, v6Unspecified) ==
								0) ? v6allnodesL : np->src, np->target,
							   ipifc->mac, pktflags);
					} else
						freeblist(bp);
					break;

				case t_unitent:
					/* not clear what needs to be done. send up
					 * an icmp mesg saying don't use this address? */

				default:
					freeblist(bp);
			}

			break;

		case NbrAdvert:
			np = (struct Ndpkt *)p;

			/* if the target address matches one of the local interface
			 * address and the local interface address has tentative bit set,
			 * then insert into ARP table. this is so the duplication address
			 * detection part of ipconfig can discover duplication through
			 * the arp table
			 */
			lifc = iplocalonifc(ipifc, np->target);
			if (lifc && lifc->tentative)
				refresh = 0;
			arpenter(icmp->f, V6, np->target, np->lnaddr, 8 * np->olen - 2,
					 refresh);
			freeblist(bp);
			break;

		case PacketTooBigV6:

		default:
			goticmpkt6(icmp, bp, 0);
			break;
	}
	return;

raise:
	freeblist(bp);

}

int icmpstats6(struct Proto *icmp6, char *buf, int len)
{
	Icmppriv6 *priv;
	char *p, *e;
	int i;

	priv = icmp6->priv;
	p = buf;
	e = p + len;
	for (i = 0; i < Nstats6; i++)
		p = seprintf(p, e, "%s: %u\n", statnames6[i], priv->stats[i]);
	for (i = 0; i <= Maxtype6; i++) {
		if (icmpnames6[i])
			p = seprintf(p, e, "%s: %u %u\n", icmpnames6[i], priv->in[i],
						 priv->out[i]);
		else
			p = seprintf(p, e, "%d: %u %u\n", i, priv->in[i], priv->out[i]);
	}
	return p - buf;
}

// need to import from icmp.c
extern int icmpstate(struct conv *c, char *state, int n);
extern void icmpannounce(struct conv *c, char **argv, int argc);
extern void icmpconnect(struct conv *c, char **argv, int argc);
extern void icmpclose(struct conv *c);

void icmp6newconv(struct Proto *icmp6, struct conv *conv)
{
}

void icmp6init(struct Fs *fs)
{
	struct Proto *icmp6 = kzmalloc(sizeof(struct Proto), 0);

	icmp6->priv = kzmalloc(sizeof(Icmppriv6), 0);
	icmp6->name = "icmpv6";
	icmp6->connect = icmpconnect;
	icmp6->announce = icmpannounce;
	icmp6->state = icmpstate;
	icmp6->create = icmpcreate6;
	icmp6->close = icmpclose;
	icmp6->rcv = icmpiput6;
	icmp6->stats = icmpstats6;
	icmp6->ctl = icmpctl6;
	icmp6->advise = icmpadvise6;
	icmp6->newconv = icmp6newconv;
	icmp6->gc = NULL;
	icmp6->ipproto = ICMPv6;
	icmp6->nc = 16;
	icmp6->ptclsize = sizeof(Icmpcb6);

	Fsproto(fs, icmp6);
}
