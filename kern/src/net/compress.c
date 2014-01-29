// INFERNO
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
#include <ip.h>

typedef struct Iphdr Iphdr;
typedef struct Tcphdr Tcphdr;
typedef struct Ilhdr Ilhdr;
typedef struct Hdr Hdr;
typedef struct Tcpc Tcpc;

struct Iphdr {
	uint8_t vihl;				/* Version and header length */
	uint8_t tos;				/* Type of service */
	uint8_t length[2];			/* packet length */
	uint8_t id[2];				/* Identification */
	uint8_t frag[2];			/* Fragment information */
	uint8_t ttl;				/* Time to live */
	uint8_t proto;				/* Protocol */
	uint8_t cksum[2];			/* Header checksum */
	uint32_t src;				/* Ip source (byte ordering unimportant) */
	uint32_t dst;				/* Ip destination (byte ordering unimportant) */
};

struct Tcphdr {
	uint32_t ports;				/* defined as a uint32_t to make comparisons easier */
	uint8_t seq[4];
	uint8_t ack[4];
	uint8_t flag[2];
	uint8_t win[2];
	uint8_t cksum[2];
	uint8_t urg[2];
};

struct Ilhdr {
	uint8_t sum[2];				/* Checksum including header */
	uint8_t len[2];				/* Packet length */
	uint8_t type;				/* Packet type */
	uint8_t spec;				/* Special */
	uint8_t src[2];				/* Src port */
	uint8_t dst[2];				/* Dst port */
	uint8_t id[4];				/* Sequence id */
	uint8_t ack[4];				/* Acked sequence */
};

enum {
	URG = 0x20,					/* Data marked urgent */
	ACK = 0x10,	/* Aknowledge is valid */
	PSH = 0x08,	/* Whole data pipe is pushed */
	RST = 0x04,	/* Reset connection */
	SYN = 0x02,	/* Pkt. is synchronise */
	FIN = 0x01,	/* Start close down */

	IP_DF = 0x4000,	/* Don't fragment */

	IP_TCPPROTO = 6,
	IP_ILPROTO = 40,
	IL_IPHDR = 20,
};

struct Hdr {
	uint8_t buf[128];
	Iphdr *ip;
	Tcphdr *tcp;
	int len;
};

struct Tcpc {
	uint8_t lastrecv;
	uint8_t lastxmit;
	uint8_t basexmit;
	uint8_t err;
	uint8_t compressid;
	Hdr t[MAX_STATES];
	Hdr r[MAX_STATES];
};

enum {							/* flag bits for what changed in a packet */
	NEW_U = (1 << 0),			/* tcp only */
	NEW_W = (1 << 1),	/* tcp only */
	NEW_A = (1 << 2),	/* il tcp */
	NEW_S = (1 << 3),	/* tcp only */
	NEW_P = (1 << 4),	/* tcp only */
	NEW_I = (1 << 5),	/* il tcp */
	NEW_C = (1 << 6),	/* il tcp */
	NEW_T = (1 << 7),	/* il only */
	TCP_PUSH_BIT = 0x10,
};

/* reserved, special-case values of above for tcp */
#define SPECIAL_I (NEW_S|NEW_W|NEW_U)	/* echoed interactive traffic */
#define SPECIAL_D (NEW_S|NEW_A|NEW_W|NEW_U)	/* unidirectional data */
#define SPECIALS_MASK (NEW_S|NEW_A|NEW_W|NEW_U)

int encode(void *p, uint32_t n)
{
	uint8_t *cp;

	cp = p;
	if (n >= 256 || n == 0) {
		*cp++ = 0;
		cp[0] = n >> 8;
		cp[1] = n;
		return 3;
	} else
		*cp = n;
	return 1;
}

#define DECODEL(f) { \
	if (*cp == 0) {\
		hnputl(f, nhgetl(f) + ((cp[1] << 8) | cp[2])); \
		cp += 3; \
	} else { \
		hnputl(f, nhgetl(f) + (uint32_t)*cp++); \
	} \
}
#define DECODES(f) { \
	if (*cp == 0) {\
		hnputs(f, nhgets(f) + ((cp[1] << 8) | cp[2])); \
		cp += 3; \
	} else { \
		hnputs(f, nhgets(f) + (uint32_t)*cp++); \
	} \
}

uint16_t tcpcompress(Tcpc * comp, struct block * b, struct Fs *)
{
	Iphdr *ip;					/* current packet */
	Tcphdr *tcp;				/* current pkt */
	uint32_t iplen, tcplen, hlen;	/* header length in bytes */
	uint32_t deltaS, deltaA;	/* general purpose temporaries */
	uint32_t changes;			/* change mask */
	uint8_t new_seq[16];		/* changes from last to current */
	uint8_t *cp;
	Hdr *h;						/* last packet */
	int i, j;

	/*
	 * Bail if this is not a compressible TCP/IP packet
	 */
	ip = (Iphdr *) b->rp;
	iplen = (ip->vihl & 0xf) << 2;
	tcp = (Tcphdr *) (b->rp + iplen);
	tcplen = (tcp->flag[0] & 0xf0) >> 2;
	hlen = iplen + tcplen;
	if ((tcp->flag[1] & (SYN | FIN | RST | ACK)) != ACK)
		return Pip;	/* connection control */

	/*
	 * Packet is compressible, look for a connection
	 */
	changes = 0;
	cp = new_seq;
	j = comp->lastxmit;
	h = &comp->t[j];
	if (ip->src != h->ip->src || ip->dst != h->ip->dst
		|| tcp->ports != h->tcp->ports) {
		for (i = 0; i < MAX_STATES; ++i) {
			j = (comp->basexmit + i) % MAX_STATES;
			h = &comp->t[j];
			if (ip->src == h->ip->src && ip->dst == h->ip->dst
				&& tcp->ports == h->tcp->ports)
				goto found;
		}

		/* no connection, reuse the oldest */
		if (i == MAX_STATES) {
			j = comp->basexmit;
			j = (j + MAX_STATES - 1) % MAX_STATES;
			comp->basexmit = j;
			h = &comp->t[j];
			goto raise;
		}
	}
found:

	/*
	 * Make sure that only what we expect to change changed. 
	 */
	if (ip->vihl != h->ip->vihl || ip->tos != h->ip->tos ||
		ip->ttl != h->ip->ttl || ip->proto != h->ip->proto)
		goto raise;	/* headers changed */
	if (iplen != sizeof(Iphdr)
		&& memcmp(ip + 1, h->ip + 1, iplen - sizeof(Iphdr)))
		goto raise;	/* ip options changed */
	if (tcplen != sizeof(Tcphdr)
		&& memcmp(tcp + 1, h->tcp + 1, tcplen - sizeof(Tcphdr)))
		goto raise;	/* tcp options changed */

	if (tcp->flag[1] & URG) {
		cp += encode(cp, nhgets(tcp->urg));
		changes |= NEW_U;
	} else if (memcmp(tcp->urg, h->tcp->urg, sizeof(tcp->urg)) != 0)
		goto raise;
	if (deltaS = nhgets(tcp->win) - nhgets(h->tcp->win)) {
		cp += encode(cp, deltaS);
		changes |= NEW_W;
	}
	if (deltaA = nhgetl(tcp->ack) - nhgetl(h->tcp->ack)) {
		if (deltaA > 0xffff)
			goto raise;
		cp += encode(cp, deltaA);
		changes |= NEW_A;
	}
	if (deltaS = nhgetl(tcp->seq) - nhgetl(h->tcp->seq)) {
		if (deltaS > 0xffff)
			goto raise;
		cp += encode(cp, deltaS);
		changes |= NEW_S;
	}

	/*
	 * Look for the special-case encodings.
	 */
	switch (changes) {
		case 0:
			/*
			 * Nothing changed. If this packet contains data and the last
			 * one didn't, this is probably a data packet following an
			 * ack (normal on an interactive connection) and we send it
			 * compressed. Otherwise it's probably a retransmit,
			 * retransmitted ack or window probe.  Send it uncompressed
			 * in case the other side missed the compressed version.
			 */
			if (nhgets(ip->length) == nhgets(h->ip->length) ||
				nhgets(h->ip->length) != hlen)
				goto raise;
			break;
		case SPECIAL_I:
		case SPECIAL_D:
			/*
			 * Actual changes match one of our special case encodings --
			 * send packet uncompressed.
			 */
			goto raise;
		case NEW_S | NEW_A:
			if (deltaS == deltaA && deltaS == nhgets(h->ip->length) - hlen) {
				/* special case for echoed terminal traffic */
				changes = SPECIAL_I;
				cp = new_seq;
			}
			break;
		case NEW_S:
			if (deltaS == nhgets(h->ip->length) - hlen) {
				/* special case for data xfer */
				changes = SPECIAL_D;
				cp = new_seq;
			}
			break;
	}
	deltaS = nhgets(ip->id) - nhgets(h->ip->id);
	if (deltaS != 1) {
		cp += encode(cp, deltaS);
		changes |= NEW_I;
	}
	if (tcp->flag[1] & PSH)
		changes |= TCP_PUSH_BIT;
	/*
	 * Grab the cksum before we overwrite it below. Then update our
	 * state with this packet's header.
	 */
	deltaA = nhgets(tcp->cksum);
	memmove(h->buf, b->rp, hlen);
	h->len = hlen;
	h->tcp = (Tcphdr *) (h->buf + iplen);

	/*
	 * We want to use the original packet as our compressed packet. (cp -
	 * new_seq) is the number of bytes we need for compressed sequence
	 * numbers. In addition we need one byte for the change mask, one
	 * for the connection id and two for the tcp checksum. So, (cp -
	 * new_seq) + 4 bytes of header are needed. hlen is how many bytes
	 * of the original packet to toss so subtract the two to get the new
	 * packet size. The temporaries are gross -egs.
	 */
	deltaS = cp - new_seq;
	cp = b->rp;
	if (comp->lastxmit != j || comp->compressid == 0) {
		comp->lastxmit = j;
		hlen -= deltaS + 4;
		cp += hlen;
		*cp++ = (changes | NEW_C);
		*cp++ = j;
	} else {
		hlen -= deltaS + 3;
		cp += hlen;
		*cp++ = changes;
	}
	b->rp += hlen;
	hnputs(cp, deltaA);
	cp += 2;
	memmove(cp, new_seq, deltaS);
	return Pvjctcp;

raise:
	/*
	 * Update connection state & send uncompressed packet
	 */
	memmove(h->buf, b->rp, hlen);
	h->tcp = (Tcphdr *) (h->buf + iplen);
	h->len = hlen;
	h->ip->proto = j;
	comp->lastxmit = j;
	return Pvjutcp;
}

struct block *tcpuncompress(Tcpc * comp, struct block *b, uint16_t type,
							struct Fs *f)
{
	uint8_t *cp, changes;
	int i;
	int iplen, len;
	Iphdr *ip;
	Tcphdr *tcp;
	Hdr *h;

	if (type == Pvjutcp) {
		/*
		 *  Locate the saved state for this connection. If the state
		 *  index is legal, clear the 'discard' flag.
		 */
		ip = (Iphdr *) b->rp;
		if (ip->proto >= MAX_STATES)
			goto raise;
		iplen = (ip->vihl & 0xf) << 2;
		tcp = (Tcphdr *) (b->rp + iplen);
		comp->lastrecv = ip->proto;
		len = iplen + ((tcp->flag[0] & 0xf0) >> 2);
		comp->err = 0;
		netlog(f, Logcompress, "uncompressed %d\n", comp->lastrecv);
		/*
		 * Restore the IP protocol field then save a copy of this
		 * packet header. The checksum is zeroed in the copy so we
		 * don't have to zero it each time we process a compressed
		 * packet.
		 */
		ip->proto = IP_TCPPROTO;
		h = &comp->r[comp->lastrecv];
		memmove(h->buf, b->rp, len);
		h->tcp = (Tcphdr *) (h->buf + iplen);
		h->len = len;
		h->ip->cksum[0] = h->ip->cksum[1] = 0;
		return b;
	}

	cp = b->rp;
	changes = *cp++;
	if (changes & NEW_C) {
		/*
		 * Make sure the state index is in range, then grab the
		 * state. If we have a good state index, clear the 'discard'
		 * flag.
		 */
		if (*cp >= MAX_STATES)
			goto raise;
		comp->err = 0;
		comp->lastrecv = *cp++;
		netlog(f, Logcompress, "newc %d\n", comp->lastrecv);
	} else {
		/*
		 * This packet has no state index. If we've had a
		 * line error since the last time we got an explicit state
		 * index, we have to toss the packet.
		 */
		if (comp->err != 0) {
			freeblist(b);
			return NULL;
		}
		netlog(f, Logcompress, "oldc %d\n", comp->lastrecv);
	}

	/*
	 * Find the state then fill in the TCP checksum and PUSH bit.
	 */
	h = &comp->r[comp->lastrecv];
	ip = h->ip;
	tcp = h->tcp;
	len = h->len;
	memmove(tcp->cksum, cp, sizeof tcp->cksum);
	cp += 2;
	if (changes & TCP_PUSH_BIT)
		tcp->flag[1] |= PSH;
	else
		tcp->flag[1] &= ~PSH;
	/*
	 * Fix up the state's ack, seq, urg and win fields based on the
	 * changemask.
	 */
	switch (changes & SPECIALS_MASK) {
		case SPECIAL_I:
			i = nhgets(ip->length) - len;
			hnputl(tcp->ack, nhgetl(tcp->ack) + i);
			hnputl(tcp->seq, nhgetl(tcp->seq) + i);
			break;

		case SPECIAL_D:
			hnputl(tcp->seq, nhgetl(tcp->seq) + nhgets(ip->length) - len);
			break;

		default:
			if (changes & NEW_U) {
				tcp->flag[1] |= URG;
				if (*cp == 0) {
					hnputs(tcp->urg, nhgets(cp + 1));
					cp += 3;
				} else {
					hnputs(tcp->urg, *cp++);
				}
			} else {
				tcp->flag[1] &= ~URG;
			}
			if (changes & NEW_W)
				DECODES(tcp->win)
					if (changes & NEW_A)
					DECODEL(tcp->ack)
						if (changes & NEW_S)
						DECODEL(tcp->seq)
							break;
	}

	/* Update the IP ID */
	if (changes & NEW_I)
		DECODES(ip->id)
			else
		hnputs(ip->id, nhgets(ip->id) + 1);

	/*
	 *  At this po int unused_int, cp points to the first byte of data in the packet.
	 *  Back up cp by the TCP/IP header length to make room for the
	 *  reconstructed header.
	 *  We assume the packet we were handed has enough space to prepend
	 *  up to 128 bytes of header.
	 */
	b->rp = cp;
	if (b->rp - b->base < len) {
		b = padblock(b, len);
		b = pullupblock(b, blocklen(b));
	} else
		b->rp -= len;
	hnputs(ip->length, BLEN(b));
	memmove(b->rp, ip, len);

	/* recompute the ip header checksum */
	ip = (Iphdr *) b->rp;
	hnputs(ip->cksum, ipcsum(b->rp));
	return b;

raise:
	netlog(f, Logcompress, "Bad Packet!\n");
	comp->err = 1;
	freeblist(b);
	return NULL;
}

Tcpc *compress_init(Tcpc * c)
{
	int i;
	Hdr *h;

	if (c == NULL) {
		c = kzmalloc(sizeof(Tcpc), 0);
		if (c == NULL)
			return NULL;
	}
	memset(c, 0, sizeof(*c));
	for (i = 0; i < MAX_STATES; i++) {
		h = &c->t[i];
		h->ip = (Iphdr *) h->buf;
		h->tcp = (Tcphdr *) (h->buf + 10);
		h->len = 20;
		h = &c->r[i];
		h->ip = (Iphdr *) h->buf;
		h->tcp = (Tcphdr *) (h->buf + 10);
		h->len = 20;
	}

	return c;
}

uint16_t compress(Tcpc * tcp, struct block * b, struct Fs * f)
{
	Iphdr *ip;

	/*
	 * Bail if this is not a compressible IP packet
	 */
	ip = (Iphdr *) b->rp;
	if ((nhgets(ip->frag) & 0x3fff) != 0)
		return Pip;

	switch (ip->proto) {
		case IP_TCPPROTO:
			return tcpcompress(tcp, b, f);
		default:
			return Pip;
	}
}

int compress_negotiate(Tcpc * tcp, uint8_t * data)
{
	if (data[0] != MAX_STATES - 1)
		return -1;
	tcp->compressid = data[1];
	return 0;
}
