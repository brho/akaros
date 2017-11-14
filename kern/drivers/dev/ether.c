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
#include <net/ip.h>

struct dev etherdevtab;

static char *devname(void)
{
	return etherdevtab.name;
}

enum {
	Type8021Q = 0x8100,			/* value of type field for 802.1[pQ] tags */
};

static struct ether *etherxx[MaxEther];	/* real controllers */
static struct ether *vlanalloc(struct ether *, int);
static void vlanoq(struct ether *, struct block *);

struct chan *etherattach(char *spec)
{
	ERRSTACK(1);
	uint32_t ctlrno;
	char *p;
	struct chan *chan;
	struct ether *ether, *vlan;
	int vlanid;

	ctlrno = 0;
	vlanid = 0;
	if (spec && *spec) {
		ctlrno = strtoul(spec, &p, 0);
		/* somebody interpret this for me. */
		if (((ctlrno == 0) && (p == spec)) ||
			(ctlrno >= MaxEther) || ((*p) && (*p != '.')))
			error(EINVAL, ERROR_FIXME);
		if (*p == '.') {	/* vlan */
			vlanid = strtoul(p + 1, &p, 0);
			if (vlanid <= 0 || vlanid > 0xFFF || *p)
				error(EINVAL, ERROR_FIXME);
		}
	}
	if ((ether = etherxx[ctlrno]) == 0)
		error(ENODEV, ERROR_FIXME);
	rlock(&ether->rwlock);
	if (waserror()) {
		runlock(&ether->rwlock);
		nexterror();
	}
	if (vlanid) {
		if (ether->max_mtu < ETHERMAXTU + ETHERHDRSIZE + 4)
			error(EFAIL, "interface cannot support 802.1 tags");
		vlan = vlanalloc(ether, vlanid);
		chan = devattach(devname(), spec);
		chan->dev = ctlrno + (vlanid << 8);
		chan->aux = vlan;
		poperror();
		runlock(&ether->rwlock);
		return chan;
	}
	chan = devattach(devname(), spec);
	chan->dev = ctlrno;
	chan->aux = ether;
	if (ether->attach)
		ether->attach(ether);
	poperror();
	runlock(&ether->rwlock);
	return chan;
}

static void ethershutdown(void)
{
	struct ether *ether;
	int i;

	for (i = 0; i < MaxEther; i++) {
		ether = etherxx[i];
		if (ether != NULL && ether->detach != NULL)
			ether->detach(ether);
	}
}

static struct walkqid *etherwalk(struct chan *chan, struct chan *nchan,
								 char **name, int nname)
{
	ERRSTACK(1);
	struct walkqid *wq;
	struct ether *ether;

	ether = chan->aux;
	rlock(&ether->rwlock);
	if (waserror()) {
		runlock(&ether->rwlock);
		nexterror();
	}
	wq = netifwalk(ether, chan, nchan, name, nname);
	if (wq && wq->clone != NULL && wq->clone != chan)
		wq->clone->aux = ether;
	poperror();
	runlock(&ether->rwlock);
	return wq;
}

static int etherstat(struct chan *chan, uint8_t * dp, int n)
{
	ERRSTACK(1);
	int s;
	struct ether *ether;

	ether = chan->aux;
	rlock(&ether->rwlock);
	if (waserror()) {
		runlock(&ether->rwlock);
		nexterror();
	}
	s = netifstat(ether, chan, dp, n);
	poperror();
	runlock(&ether->rwlock);
	return s;
}

static struct chan *etheropen(struct chan *chan, int omode)
{
	ERRSTACK(1);
	struct chan *c;
	struct ether *ether;

	ether = chan->aux;
	rlock(&ether->rwlock);
	if (waserror()) {
		runlock(&ether->rwlock);
		nexterror();
	}
	c = netifopen(ether, chan, omode);
	poperror();
	runlock(&ether->rwlock);
	return c;
}

static void etherclose(struct chan *chan)
{
	ERRSTACK(1);
	struct ether *ether;

	ether = chan->aux;
	rlock(&ether->rwlock);
	if (waserror()) {
		runlock(&ether->rwlock);
		nexterror();
	}
	netifclose(ether, chan);
	poperror();
	runlock(&ether->rwlock);
}

static long etherread(struct chan *chan, void *buf, long n, int64_t off)
{
	ERRSTACK(1);
	struct ether *ether;
	uint32_t offset = off;
	long r;

	ether = chan->aux;
	rlock(&ether->rwlock);
	if (waserror()) {
		runlock(&ether->rwlock);
		nexterror();
	}
	if ((chan->qid.type & QTDIR) == 0 && ether->ifstat) {
		/*
		 * With some controllers it is necessary to reach
		 * into the chip to extract statistics.
		 */
		if (NETTYPE(chan->qid.path) == Nifstatqid) {
			r = ether->ifstat(ether, buf, n, offset);
			goto out;
		}
		if (NETTYPE(chan->qid.path) == Nstatqid)
			ether->ifstat(ether, buf, 0, offset);
	}
	r = netifread(ether, chan, buf, n, offset);
out:
	poperror();
	runlock(&ether->rwlock);
	return r;
}

static struct block *etherbread(struct chan *chan, long n, uint32_t offset)
{
	ERRSTACK(1);
	struct block *b;
	struct ether *ether;

	ether = chan->aux;
	rlock(&ether->rwlock);
	if (waserror()) {
		runlock(&ether->rwlock);
		nexterror();
	}
	b = netifbread(ether, chan, n, offset);
	poperror();
	runlock(&ether->rwlock);
	return b;
}

static int etherwstat(struct chan *chan, uint8_t * dp, int n)
{
	ERRSTACK(1);
	struct ether *ether;
	int r;

	ether = chan->aux;
	rlock(&ether->rwlock);
	if (waserror()) {
		runlock(&ether->rwlock);
		nexterror();
	}
	r = netifwstat(ether, chan, dp, n);
	poperror();
	runlock(&ether->rwlock);
	return r;
}

static void etherrtrace(struct netfile *f, struct etherpkt *pkt, int len)
{
	uint64_t i, n;
	struct block *bp;

	if (qwindow(f->in) <= 0)
		return;
	if (len > 58)
		n = 58;
	else
		n = len;
	bp = block_alloc(68, MEM_ATOMIC);
	if (bp == NULL)
		return;
	memmove(bp->wp, pkt->d, n);
	/* we're storing 8 bytes here (64 bit); old 9ns was 32 bit for msec */
	i = milliseconds();
	bp->wp[58] = len >> 8;
	bp->wp[59] = len;
	bp->wp[60] = i >> 56;
	bp->wp[61] = i >> 48;
	bp->wp[62] = i >> 40;
	bp->wp[63] = i >> 32;
	bp->wp[64] = i >> 24;
	bp->wp[65] = i >> 16;
	bp->wp[66] = i >> 8;
	bp->wp[67] = i;
	bp->wp += 68;
	qpass(f->in, bp);
}

#ifdef CONFIG_RISCV
#warning "Potentially unaligned ethernet addrs!"
#endif

static inline int eaddrcmp(uint8_t *x, uint8_t *y)
{
	uint16_t *a = (uint16_t *)x;
	uint16_t *b = (uint16_t *)y;

	return (a[0] ^ b[0]) | (a[1] ^ b[1]) | (a[2] ^ b[2]);
}

struct block *etheriq(struct ether *ether, struct block *bp, int fromwire)
{
	struct etherpkt *pkt;
	uint16_t type;
	int multi, tome, fromme, vlanid, i;
	struct netfile **ep, *f, **fp, *fx;
	struct block *xbp;
	struct ether *vlan;

	ether->inpackets++;

	pkt = (struct etherpkt *)bp->rp;
	/* TODO: we might need to assert more for higher layers, or otherwise deal
	 * with extra data. */
	assert(BHLEN(bp) >= offsetof(struct etherpkt, data));
	type = (pkt->type[0] << 8) | pkt->type[1];
	if (type == Type8021Q && ether->nvlan) {
		vlanid = nhgets(bp->rp + 2 * Eaddrlen + 2) & 0xFFF;
		if (vlanid) {
			for (i = 0; i < ARRAY_SIZE(ether->vlans); i++) {
				vlan = ether->vlans[i];
				if (vlan != NULL && vlan->vlanid == vlanid) {
					/* might have a problem with extra data here */
					assert(BHLEN(bp) >= 4 + 2 * Eaddrlen);
					memmove(bp->rp + 4, bp->rp, 2 * Eaddrlen);
					bp->rp += 4;
					return etheriq(vlan, bp, fromwire);
				}
			}
			/* allow normal type handling to accept or discard it */
		}
	}

	fx = 0;
	ep = &ether->f[Ntypes];

	multi = pkt->d[0] & 1;
	/* check for valid multcast addresses */
	if (multi && eaddrcmp(pkt->d, ether->bcast) != 0
		&& ether->prom == 0) {
		if (!activemulti(ether, pkt->d, sizeof(pkt->d))) {
			if (fromwire) {
				freeb(bp);
				bp = 0;
			}
			return bp;
		}
	}

	/* is it for me? */
	tome = eaddrcmp(pkt->d, ether->ea) == 0;
	fromme = eaddrcmp(pkt->s, ether->ea) == 0;

	/*
	 * Multiplex the packet to all the connections which want it.
	 * If the packet is not to be used subsequently (fromwire != 0),
	 * attempt to simply pass it into one of the connections, thereby
	 * saving a copy of the data (usual case hopefully).
	 */
	for (fp = ether->f; fp < ep; fp++) {
		if ((f = *fp) && (f->type == type || f->type < 0))
			if (tome || multi || f->prom) {
				/* Don't want to hear bridged packets */
				if (f->bridge && !fromwire && !fromme)
					continue;
				if (f->headersonly) {
					etherrtrace(f, pkt, BHLEN(bp));
					continue;
				}
				if (fromwire && fx == 0) {
					fx = f;
					continue;
				}
				xbp = copyblock(bp, MEM_ATOMIC);
				if (xbp == 0) {
					ether->soverflows++;
					continue;
				}
				if (qpass(f->in, xbp) < 0)
					ether->soverflows++;
			}
	}

	if (fx) {
		if (qpass(fx->in, bp) < 0)
			ether->soverflows++;
		return 0;
	}
	if (fromwire) {
		freeb(bp);
		return 0;
	}

	return bp;
}

static int etheroq(struct ether *ether, struct block *bp)
{
	int len, loopback;
	struct etherpkt *pkt;
	int8_t irq_state = 0;

	ether->outpackets++;

	if (!(ether->feat & NETF_SG))
		bp = linearizeblock(bp);
	ptclcsum_finalize(bp, ether->feat);
	/*
	 * Check if the packet has to be placed back onto the input queue,
	 * i.e. if it's a loopback or broadcast packet or the interface is
	 * in promiscuous mode.
	 * If it's a loopback packet indicate to etheriq that the data isn't
	 * needed and return, etheriq will pass-on or free the block.
	 * To enable bridging to work, only packets that were originated
	 * by this interface are fed back.
	 */
	pkt = (struct etherpkt *)bp->rp;
	len = BLEN(bp);
	loopback = eaddrcmp(pkt->d, ether->ea) == 0;
	if (loopback || eaddrcmp(pkt->d, ether->bcast) == 0 || ether->prom) {
		disable_irqsave(&irq_state);
		etheriq(ether, bp, 0);
		enable_irqsave(&irq_state);
		if (loopback) {
			freeb(bp);
			return len;
		}
	}

	if (ether->vlanid) {
		/* add tag */
		bp = padblock(bp, 2 + 2);
		memmove(bp->rp, bp->rp + 4, 2 * Eaddrlen);
		hnputs(bp->rp + 2 * Eaddrlen, Type8021Q);
		hnputs(bp->rp + 2 * Eaddrlen + 2, ether->vlanid & 0xFFF);	/* prio:3 0:1 vid:12 */
		ether = ether->ctlr;
	}

	if ((ether->feat & NETF_PADMIN) == 0 && BLEN(bp) < ether->min_mtu)
		bp = adjustblock(bp, ether->min_mtu);

	qbwrite(ether->oq, bp);
	if (ether->transmit != NULL)
		ether->transmit(ether);

	return len;
}

static long etherwrite(struct chan *chan, void *buf, long n, int64_t unused)
{
	ERRSTACK(2);
	struct ether *ether;
	struct block *bp;
	int onoff;
	struct cmdbuf *cb;
	long l;

	ether = chan->aux;
	rlock(&ether->rwlock);
	if (waserror()) {
		runlock(&ether->rwlock);
		nexterror();
	}
	if (NETTYPE(chan->qid.path) != Ndataqid) {
		l = netifwrite(ether, chan, buf, n);
		if (l >= 0)
			goto out;
		cb = parsecmd(buf, n);
		if (cb->nf < 1) {
			kfree(cb);
			error(EFAIL, "short control request");
		}
		if (strcmp(cb->f[0], "nonblocking") == 0) {
			if (cb->nf <= 1)
				onoff = 1;
			else
				onoff = atoi(cb->f[1]);
			if (ether->oq != NULL)
				qdropoverflow(ether->oq, onoff);
			kfree(cb);
			goto out;
		}
		kfree(cb);
		if (ether->ctl != NULL) {
			l = ether->ctl(ether, buf, n);
			goto out;
		}
		error(EINVAL, ERROR_FIXME);
	}

	if (n > ether->mtu + ETHERHDRSIZE)
		error(E2BIG, ERROR_FIXME);
	bp = block_alloc(n, MEM_WAIT);
	if (waserror()) {
		freeb(bp);
		nexterror();
	}
	memmove(bp->rp, buf, n);
	memmove(bp->rp + Eaddrlen, ether->ea, Eaddrlen);
	bp->wp += n;
	poperror();

	l = etheroq(ether, bp);
out:
	poperror();
	runlock(&ether->rwlock);
	return l;
}

static long etherbwrite(struct chan *chan, struct block *bp, uint32_t unused)
{
	ERRSTACK(1);
	struct ether *ether;
	long n;

	n = BLEN(bp);
	if (NETTYPE(chan->qid.path) != Ndataqid) {
		if (waserror()) {
			freeb(bp);
			nexterror();
		}
		n = etherwrite(chan, bp->rp, n, 0);
		poperror();
		freeb(bp);
		return n;
	}
	ether = chan->aux;
	rlock(&ether->rwlock);
	if (waserror()) {
		runlock(&ether->rwlock);
		nexterror();
	}
	if (n > ether->mtu + ETHERHDRSIZE && (bp->flag & Btso) == 0) {
		freeb(bp);
		error(E2BIG, ERROR_FIXME);
	}
	n = etheroq(ether, bp);
	poperror();
	runlock(&ether->rwlock);
	return n;
}

static void nop(struct ether *unused)
{
}

static long vlanctl(struct ether *ether, void *buf, long n)
{
	uint8_t ea[Eaddrlen];
	struct ether *master;
	struct cmdbuf *cb;
	int i;

	cb = parsecmd(buf, n);
	if (cb->nf >= 2
		&& strcmp(cb->f[0], "ea") == 0 && parseether(ea, cb->f[1]) == 0) {
		kfree(cb);
		memmove(ether->ea, ea, Eaddrlen);
		memmove(ether->addr, ether->ea, Eaddrlen);
		return 0;
	}
	if (cb->nf == 1 && strcmp(cb->f[0], "disable") == 0) {
		master = ether->ctlr;
		qlock(&master->vlq);
		for (i = 0; i < ARRAY_SIZE(master->vlans); i++)
			if (master->vlans[i] == ether) {
				ether->vlanid = 0;
				master->nvlan--;
				break;
			}
		qunlock(&master->vlq);
		kfree(cb);
		return 0;
	}
	kfree(cb);
	error(EINVAL, ERROR_FIXME);
	return -1;	/* not reached */
}

static struct ether *vlanalloc(struct ether *ether, int id)
{
	ERRSTACK(1);
	struct ether *vlan;
	int i, fid;
	char name[KNAMELEN];

	qlock(&ether->vlq);
	if (waserror()) {
		qunlock(&ether->vlq);
		nexterror();
	}
	fid = -1;
	for (i = 0; i < ARRAY_SIZE(ether->vlans); i++) {
		vlan = ether->vlans[i];
		if (vlan != NULL && vlan->vlanid == id) {
			poperror();
			qunlock(&ether->vlq);
			return vlan;
		}
		if (fid < 0 && (vlan == NULL || vlan->vlanid == 0))
			fid = i;
	}
	if (fid < 0)
		error(ENOENT, ERROR_FIXME);
	snprintf(name, sizeof(name), "ether%d.%d", ether->ctlrno, id);
	vlan = ether->vlans[fid];
	if (vlan == NULL) {
		vlan = kzmalloc(sizeof(struct ether), 1);
		if (vlan == NULL)
			error(ENOMEM, ERROR_FIXME);
		rwinit(&vlan->rwlock);
		qlock_init(&vlan->vlq);
		netifinit(vlan, name, Ntypes, ether->limit);
		ether->vlans[fid] = vlan;	/* id is still zero, can't be matched */
		ether->nvlan++;
	} else
		memmove(vlan->name, name, KNAMELEN - 1);
	vlan->attach = nop;
	vlan->transmit = NULL;
	vlan->ctl = vlanctl;
	vlan->irq = -1;
	vlan->promiscuous = ether->promiscuous;
	vlan->multicast = ether->multicast;
	vlan->arg = vlan;
	vlan->mbps = ether->mbps;
	vlan->fullduplex = ether->fullduplex;
	vlan->encry = ether->encry;
	vlan->mtu = ether->mtu;
	vlan->min_mtu = ether->min_mtu;
	vlan->max_mtu = ether->max_mtu;
	vlan->ctlrno = ether->ctlrno;
	vlan->vlanid = id;
	vlan->alen = Eaddrlen;
	memmove(vlan->addr, ether->addr, sizeof(vlan->addr));
	memmove(vlan->bcast, ether->bcast, sizeof(ether->bcast));
	vlan->oq = NULL;
	vlan->ctlr = ether;
	vlan->vlanid = id;
	poperror();
	qunlock(&ether->vlq);
	return vlan;
}

static struct {
	char *type;
	int (*reset) (struct ether *);
} cards[MaxEther + 1];

void addethercard(char *t, int (*r) (struct ether *))
{
	static int ncard;

	if (ncard == MaxEther)
		panic("too many ether cards");
	cards[ncard].type = t;
	cards[ncard].reset = r;
	ncard++;
}

int parseether(uint8_t * to, char *from)
{
	char nip[4];
	char *p;
	int i;

	p = from;
	for (i = 0; i < Eaddrlen; i++) {
		if (*p == 0)
			return -1;
		nip[0] = *p++;
		if (*p == 0)
			return -1;
		nip[1] = *p++;
		nip[2] = 0;
		to[i] = strtoul(nip, 0, 16);
		if (*p == ':')
			p++;
	}
	return 0;
}

static void etherreset(void)
{
	struct ether *ether;
	int i, n, ctlrno, qsize;
	char name[KNAMELEN], buf[128];

	for (ether = 0, ctlrno = 0; ctlrno < MaxEther; ctlrno++) {
		if (ether == 0)
			ether = kzmalloc(sizeof(struct ether), 0);
		memset(ether, 0, sizeof(struct ether));
		rwinit(&ether->rwlock);
		qlock_init(&ether->vlq);
		rendez_init(&ether->link_rz);
		ether->ctlrno = ctlrno;
		ether->mbps = 10;
		ether->mtu = ETHERMAXTU;
		ether->min_mtu = ETHERMINTU;
		ether->max_mtu = ETHERMAXTU;
		/* looked like irq type, we don't have these yet */
		//ether->netif.itype = -1;

		/* TODO: looks like they expected some init to be done here.  at the
		 * very least, ether->type is 0 right now, and needs to be set.  looking
		 * around online, it seems to find out ether config settings, so that we
		 * can set some flags in the opt parseing below. */
		//if(archether(ctlrno, ether) <= 0)
		//  continue;

		for (n = 0; cards[n].type; n++) {
#if 0
			if (cistrcmp(cards[n].type, ether->type))
				continue;
			for (i = 0; i < ether->nopt; i++) {
				if (cistrncmp(ether->opt[i], "ea=", 3) == 0) {
					if (parseether(ether->ea, &ether->opt[i][3]) == -1)
						memset(ether->ea, 0, Eaddrlen);
				} else if (cistrcmp(ether->opt[i], "fullduplex") == 0 ||
						   cistrcmp(ether->opt[i], "10BASE-TFD") == 0)
					ether->fullduplex = 1;
				else if (cistrcmp(ether->opt[i], "100BASE-TXFD") == 0)
					ether->mbps = 100;
			}
#endif
			if (cards[n].reset(ether))
				continue;
			/* might be fucked a bit - reset() doesn't know the type.  might not
			 * even matter, except for debugging. */
			ether->type = cards[n].type;
			snprintf(name, sizeof(name), "ether%d", ctlrno);

			i = snprintf(buf, sizeof(buf),
						 "#l%d: %s: %dMbps port 0x%x irq %u", ctlrno,
						 ether->type, ether->mbps,
				     ether->port,
						 ether->irq);
			/* Looks like this is for printing MMIO addrs */
#if 0
			if (ether->mem)
				i += snprintf(buf + i, sizeof(buf) - i, " addr 0x%lx",
							  PADDR(ether->mem));
			if (ether->size)
				i += snprintf(buf + i, sizeof(buf) - i, " size 0x%lx",
							  ether->size);
#endif
			i += snprintf(buf + i, sizeof(buf) - i,
						  ": %02.2x:%02.2x:%02.2x:%02.2x:%02.2x:%02.2x",
						  ether->ea[0], ether->ea[1], ether->ea[2],
						  ether->ea[3], ether->ea[4], ether->ea[5]);
			snprintf(buf + i, sizeof(buf) - i, "\n");
			printk(buf);

			switch (ether->mbps) {

			case 1 ... 99:
				qsize = 64 * 1024;
				break;
			case 100 ... 999:
				qsize = 256 * 1024;
				break;
			case 1000 ... 9999:
				qsize = 1024 * 1024;
				break;
			default:
				qsize = 8 * 1024 * 1024;
			}
			netifinit(ether, name, Ntypes, qsize);
			if (ether->oq == 0)
				ether->oq = qopen(qsize, Qmsg, 0, 0);
			if (ether->oq == 0)
				panic("etherreset %s", name);
			ether->alen = Eaddrlen;
			memmove(ether->addr, ether->ea, Eaddrlen);
			memset(ether->bcast, 0xFF, Eaddrlen);

			etherxx[ctlrno] = ether;
			ether = 0;
			break;
		}
	}
	if (ether)
		kfree(ether);
}

static void etherpower(int on)
{
	int i;
	struct ether *ether;

	/* TODO: fix etherpower.  locking and ether->readers are broken. */
	warn("%s needs attention.  had a rough porting from inferno", __FUNCTION__);
	for (i = 0; i < MaxEther; i++) {
		if ((ether = etherxx[i]) == NULL || ether->power == NULL)
			continue;
		if (on) {
			/* brho: not sure what they are doing.  there seem to be certain
			 * assumptions about calling etherpower.  i think they are using
			 * canrlock to see if the lock is currently writelocked.  and if it
			 * was not lockable, they would assume they had the write lock and
			 * could unlock.  this is super fucked up. */
			if (canrlock(&ether->rwlock)) {
				runlock(&ether->rwlock);	// brho added this
				continue;
			}
			if (ether->power != NULL)
				ether->power(ether, on);
			wunlock(&ether->rwlock);
		} else {
			/* readers isn't in the ether struct...
			   if(ether->readers)
			   continue;
			 */
			wlock(&ether->rwlock);
			if (ether->power != NULL)
				ether->power(ether, on);
			/* Keep locked until power goes back on */
		}
	}
}

#define ETHERPOLY 0xedb88320

/* really slow 32 bit crc for ethers */
uint32_t ethercrc(uint8_t * p, int len)
{
	int i, j;
	uint32_t crc, b;

	crc = 0xffffffff;
	for (i = 0; i < len; i++) {
		b = *p++;
		for (j = 0; j < 8; j++) {
			crc = (crc >> 1) ^ (((crc ^ b) & 1) ? ETHERPOLY : 0);
			b >>= 1;
		}
	}
	return crc;
}

struct dev etherdevtab __devtab = {
	.name = "ether",

	.reset = etherreset,
	.init = devinit,
	.shutdown = ethershutdown,
	.attach = etherattach,
	.walk = etherwalk,
	.stat = etherstat,
	.open = etheropen,
	.create = devcreate,
	.close = etherclose,
	.read = etherread,
	.bread = etherbread,
	.write = etherwrite,
	.bwrite = etherbwrite,
	.remove = devremove,
	.wstat = etherwstat,
	.power = etherpower,
	.chaninfo = devchaninfo,
};
