#include <vfs.h>
#include <kfs.h>
#include <slab.h>
#include <kmalloc.h>
#include <kref.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <error.h>
#include <cpio.h>
#include <pmap.h>
#include <smp.h>

char *v6hdrtypes[Maxhdrtype] = {
	[HBH] "HopbyHop",
	[ICMP] "ICMP",
	[IGMP] "IGMP",
	[GGP] "GGP",
	[IPINIP] "IP",
	[ST] "ST",
	[TCP] "TCP",
	[UDP] "UDP",
	[ISO_TP4] "ISO_TP4",
	[RH] "Routinghdr",
	[FH] "Fraghdr",
	[IDRP] "IDRP",
	[RSVP] "RSVP",
	[AH] "Authhdr",
	[ESP] "ESP",
	[ICMPv6] "ICMPv6",
	[NNH] "Nonexthdr",
	[ISO_IP] "ISO_IP",
	[IGRP] "IGRP",
	[OSPF] "OSPF",
};

/*
 *  well known IPv6 addresses
 */
uint8_t v6Unspecified[IPaddrlen] = {
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0
};

uint8_t v6loopback[IPaddrlen] = {
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0x01
};

uint8_t v6linklocal[IPaddrlen] = {
	0xfe, 0x80, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0
};

uint8_t v6linklocalmask[IPaddrlen] = {
	0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff,
	0, 0, 0, 0,
	0, 0, 0, 0
};

int v6llpreflen = 8;			/* link-local prefix length in bytes */

uint8_t v6multicast[IPaddrlen] = {
	0xff, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0
};

uint8_t v6multicastmask[IPaddrlen] = {
	0xff, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0
};

int v6mcpreflen = 1;			/* multicast prefix length */

uint8_t v6allnodesN[IPaddrlen] = {
	0xff, 0x01, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0x01
};

uint8_t v6allroutersN[IPaddrlen] = {
	0xff, 0x01, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0x02
};

uint8_t v6allnodesNmask[IPaddrlen] = {
	0xff, 0xff, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0
};

int v6aNpreflen = 2;			/* all nodes (N) prefix */

uint8_t v6allnodesL[IPaddrlen] = {
	0xff, 0x02, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0x01
};

uint8_t v6allroutersL[IPaddrlen] = {
	0xff, 0x02, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0x02
};

uint8_t v6allnodesLmask[IPaddrlen] = {
	0xff, 0xff, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0
};

int v6aLpreflen = 2;			/* all nodes (L) prefix */

uint8_t v6solicitednode[IPaddrlen] = {
	0xff, 0x02, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0x01,
	0xff, 0, 0, 0
};

uint8_t v6solicitednodemask[IPaddrlen] = {
	0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff,
	0xff, 0x0, 0x0, 0x0
};

int v6snpreflen = 13;

uint16_t ptclcsum(struct block *bp, int offset, int len)
{
	uint8_t *addr;
	uint32_t losum, hisum;
	uint16_t csum;
	int odd, blocklen, x;

	/* Correct to front of data area */
	while (bp != NULL && offset && offset >= BLEN(bp)) {
		offset -= BLEN(bp);
		bp = bp->next;
	}
	if (bp == NULL)
		return 0;

	addr = bp->rp + offset;
	blocklen = BLEN(bp) - offset;

	if (bp->next == NULL) {
		if (blocklen < len)
			len = blocklen;
		return ~ptclbsum(addr, len) & 0xffff;
	}

	losum = 0;
	hisum = 0;

	odd = 0;
	while (len) {
		x = blocklen;
		if (len < x)
			x = len;

		csum = ptclbsum(addr, x);
		if (odd)
			hisum += csum;
		else
			losum += csum;
		odd = (odd + x) & 1;
		len -= x;

		bp = bp->next;
		if (bp == NULL)
			break;
		blocklen = BLEN(bp);
		addr = bp->rp;
	}

	losum += hisum >> 8;
	losum += (hisum & 0xff) << 8;
	while ((csum = losum >> 16) != 0)
		losum = csum + (losum & 0xffff);

	return ~losum & 0xffff;
}

enum {
	Isprefix = 16,
};

void ipv62smcast(uint8_t * smcast, uint8_t * a)
{
	assert(IPaddrlen == 16);
	memmove(smcast, v6solicitednode, IPaddrlen);
	smcast[13] = a[13];
	smcast[14] = a[14];
	smcast[15] = a[15];
}

/*
 *  parse a hex mac address
 */
int parsemac(uint8_t * to, char *from, int len)
{
	char nip[4];
	char *p;
	int i;

	p = from;
	memset(to, 0, len);
	for (i = 0; i < len; i++) {
		if (p[0] == '\0' || p[1] == '\0')
			break;

		nip[0] = p[0];
		nip[1] = p[1];
		nip[2] = '\0';
		p += 2;

		to[i] = strtoul(nip, 0, 16);
		if (*p == ':')
			p++;
	}
	return i;
}

/*
 *  hashing tcp, udp, ... connections
 */
uint32_t iphash(uint8_t * sa, uint16_t sp, uint8_t * da, uint16_t dp)
{
	return ((sa[IPaddrlen - 1] << 24) ^ (sp << 16) ^ (da[IPaddrlen - 1] << 8) ^
			dp) % Nhash;
}

void iphtadd(struct Ipht *ht, struct conv *c)
{
	uint32_t hv;
	struct iphash *h;

	hv = iphash(c->raddr, c->rport, c->laddr, c->lport);
	h = kmalloc(sizeof(*h), 0);
	if (ipcmp(c->raddr, IPnoaddr) != 0)
		h->match = IPmatchexact;
	else {
		if (ipcmp(c->laddr, IPnoaddr) != 0) {
			if (c->lport == 0)
				h->match = IPmatchaddr;
			else
				h->match = IPmatchpa;
		} else {
			if (c->lport == 0)
				h->match = IPmatchany;
			else
				h->match = IPmatchport;
		}
	}
	h->c = c;

	spin_lock(&ht->rwlock);
	h->next = ht->tab[hv];
	ht->tab[hv] = h;
	spin_unlock(&ht->rwlock);
}

void iphtrem(struct Ipht *ht, struct conv *c)
{
	uint32_t hv;
	struct iphash **l, *h;

	hv = iphash(c->raddr, c->rport, c->laddr, c->lport);
	spin_lock(&ht->rwlock);
	for (l = &ht->tab[hv]; (*l) != NULL; l = &(*l)->next)
		if ((*l)->c == c) {
			h = *l;
			(*l) = h->next;
			kfree(h);
			break;
		}
	spin_unlock(&ht->rwlock);
}

/* look for a matching conversation with the following precedence
 *	connected && raddr,rport,laddr,lport
 *	announced && laddr,lport
 *	announced && *,lport
 *	announced && laddr,*
 *	announced && *,*
 */
struct conv *iphtlook(struct Ipht *ht, uint8_t * sa, uint16_t sp, uint8_t * da,
					  uint16_t dp)
{
	uint32_t hv;
	struct iphash *h;
	struct conv *c;

	/* exact 4 pair match (connection) */
	hv = iphash(sa, sp, da, dp);
	spin_lock(&ht->rwlock);
	for (h = ht->tab[hv]; h != NULL; h = h->next) {
		if (h->match != IPmatchexact)
			continue;
		c = h->c;
		if (sp == c->rport && dp == c->lport
			&& ipcmp(sa, c->raddr) == 0 && ipcmp(da, c->laddr) == 0) {
			spin_unlock(&ht->rwlock);
			return c;
		}
	}

	/* match local address and port */
	hv = iphash(IPnoaddr, 0, da, dp);
	for (h = ht->tab[hv]; h != NULL; h = h->next) {
		if (h->match != IPmatchpa)
			continue;
		c = h->c;
		if (dp == c->lport && ipcmp(da, c->laddr) == 0) {
			spin_unlock(&ht->rwlock);
			return c;
		}
	}

	/* match just port */
	hv = iphash(IPnoaddr, 0, IPnoaddr, dp);
	for (h = ht->tab[hv]; h != NULL; h = h->next) {
		if (h->match != IPmatchport)
			continue;
		c = h->c;
		if (dp == c->lport) {
			spin_unlock(&ht->rwlock);
			return c;
		}
	}

	/* match local address */
	hv = iphash(IPnoaddr, 0, da, 0);
	for (h = ht->tab[hv]; h != NULL; h = h->next) {
		if (h->match != IPmatchaddr)
			continue;
		c = h->c;
		if (ipcmp(da, c->laddr) == 0) {
			spin_unlock(&ht->rwlock);
			return c;
		}
	}

	/* look for something that matches anything */
	hv = iphash(IPnoaddr, 0, IPnoaddr, 0);
	for (h = ht->tab[hv]; h != NULL; h = h->next) {
		if (h->match != IPmatchany)
			continue;
		c = h->c;
		spin_unlock(&ht->rwlock);
		return c;
	}
	spin_unlock(&ht->rwlock);
	return NULL;
}
