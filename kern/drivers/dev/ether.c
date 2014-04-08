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
			error(Ebadarg);
		if (*p == '.') {	/* vlan */
			vlanid = strtoul(p + 1, &p, 0);
			if (vlanid <= 0 || vlanid > 0xFFF || *p)
				error(Ebadarg);
		}
	}
	if ((ether = etherxx[ctlrno]) == 0)
		error(Enodev);
	rlock(&ether->rwlock);
	if (waserror()) {
		runlock(&ether->rwlock);
		nexterror();
	}
	if (vlanid) {
		if (ether->maxmtu < ETHERMAXTU + 4)
			error("interface cannot support 802.1 tags");
		vlan = vlanalloc(ether, vlanid);
		chan = devattach('l', spec);
		chan->dev = ctlrno + (vlanid << 8);
		chan->aux = vlan;
		poperror();
		runlock(&ether->rwlock);
		return chan;
	}
	chan = devattach('l', spec);
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
	wq = netifwalk(&ether->netif, chan, nchan, name, nname);
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
	s = netifstat(&ether->netif, chan, dp, n);
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
	c = netifopen(&ether->netif, chan, omode);
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
	netifclose(&ether->netif, chan);
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
	r = netifread(&ether->netif, chan, buf, n, offset);
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
	b = netifbread(&ether->netif, chan, n, offset);
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
	r = netifwstat(&ether->netif, chan, dp, n);
	poperror();
	runlock(&ether->rwlock);
	return r;
}

static void etherrtrace(struct netfile *f, struct etherpkt *pkt, int len)
{
	int i, n;
	struct block *bp;

	if (qwindow(f->in) <= 0)
		return;
	if (len > 58)
		n = 58;
	else
		n = len;
	bp = iallocb(64);
	if (bp == NULL)
		return;
	memmove(bp->wp, pkt->d, n);
	i = milliseconds();
	bp->wp[58] = len >> 8;
	bp->wp[59] = len;
	bp->wp[60] = i >> 24;
	bp->wp[61] = i >> 16;
	bp->wp[62] = i >> 8;
	bp->wp[63] = i;
	bp->wp += 64;
	qpass(f->in, bp);
}

struct block *etheriq(struct ether *ether, struct block *bp, int fromwire)
{
	struct etherpkt *pkt;
	uint16_t type;
	int len, multi, tome, fromme, vlanid, i;
	struct netfile **ep, *f, **fp, *fx;
	struct block *xbp;
	struct ether *vlan;

	ether->netif.inpackets++;

	pkt = (struct etherpkt *)bp->rp;
	len = BLEN(bp);
	type = (pkt->type[0] << 8) | pkt->type[1];
	if (type == Type8021Q && ether->nvlan) {
		vlanid = nhgets(bp->rp + 2 * Eaddrlen + 2) & 0xFFF;
		if (vlanid) {
			for (i = 0; i < ARRAY_SIZE(ether->vlans); i++) {
				vlan = ether->vlans[i];
				if (vlan != NULL && vlan->vlanid == vlanid) {
					memmove(bp->rp + 4, bp->rp, 2 * Eaddrlen);
					bp->rp += 4;
					return etheriq(vlan, bp, fromwire);
				}
			}
			/* allow normal type handling to accept or discard it */
		}
	}

	fx = 0;
	ep = &ether->netif.f[Ntypes];

	multi = pkt->d[0] & 1;
	/* check for valid multcast addresses */
	if (multi && memcmp(pkt->d, ether->netif.bcast, sizeof(pkt->d)) != 0
		&& ether->netif.prom == 0) {
		if (!activemulti(&ether->netif, pkt->d, sizeof(pkt->d))) {
			if (fromwire) {
				freeb(bp);
				bp = 0;
			}
			return bp;
		}
	}

	/* is it for me? */
	tome = memcmp(pkt->d, ether->ea, sizeof(pkt->d)) == 0;
	fromme = memcmp(pkt->s, ether->ea, sizeof(pkt->s)) == 0;

	/*
	 * Multiplex the packet to all the connections which want it.
	 * If the packet is not to be used subsequently (fromwire != 0),
	 * attempt to simply pass it into one of the connections, thereby
	 * saving a copy of the data (usual case hopefully).
	 */
	for (fp = ether->netif.f; fp < ep; fp++) {
		if ((f = *fp) && (f->type == type || f->type < 0))
			if (tome || multi || f->prom) {
				/* Don't want to hear bridged packets */
				if (f->bridge && !fromwire && !fromme)
					continue;
				if (!f->headersonly) {
					if (fromwire && fx == 0)
						fx = f;
					else if ((xbp = iallocb(len))) {
						memmove(xbp->wp, pkt, len);
						xbp->wp += len;
						if (qpass(f->in, xbp) < 0)
							ether->netif.soverflows++;
					} else
						ether->netif.soverflows++;
				} else
					etherrtrace(f, pkt, len);
			}
	}

	if (fx) {
		if (qpass(fx->in, bp) < 0)
			ether->netif.soverflows++;
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

	ether->netif.outpackets++;

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
	loopback = memcmp(pkt->d, ether->ea, sizeof(pkt->d)) == 0;
	if (loopback || memcmp(pkt->d, ether->netif.bcast, sizeof(pkt->d)) == 0
		|| ether->netif.prom) {
		disable_irqsave(&irq_state);
		etheriq(ether, bp, 0);
		enable_irqsave(&irq_state);
	}

	if (!loopback) {
		if (ether->vlanid) {
			/* add tag */
			bp = padblock(bp, 2 + 2);
			memmove(bp->rp, bp->rp + 4, 2 * Eaddrlen);
			hnputs(bp->rp + 2 * Eaddrlen, Type8021Q);
			hnputs(bp->rp + 2 * Eaddrlen + 2, ether->vlanid & 0xFFF);	/* prio:3 0:1 vid:12 */
			ether = ether->ctlr;
		}
		qbwrite(ether->oq, bp);
		if (ether->transmit != NULL)
			ether->transmit(ether);
	} else
		freeb(bp);

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
		l = netifwrite(&ether->netif, chan, buf, n);
		if (l >= 0)
			goto out;
		cb = parsecmd(buf, n);
		if (strcmp(cb->f[0], "nonblocking") == 0) {
			if (cb->nf <= 1)
				onoff = 1;
			else
				onoff = atoi(cb->f[1]);
			if (ether->oq != NULL)
				qnoblock(ether->oq, onoff);
			kfree(cb);
			goto out;
		}
		kfree(cb);
		if (ether->ctl != NULL) {
			l = ether->ctl(ether, buf, n);
			goto out;
		}
		error(Ebadctl);
	}

	if (n > ether->maxmtu)
		error(Etoobig);
	if (n < ether->minmtu)
		error(Etoosmall);
	bp = allocb(n);
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
	if (n > ether->maxmtu) {
		freeb(bp);
		error(Etoobig);
	}
	if (n < ether->minmtu) {
		freeb(bp);
		error(Etoosmall);
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
		memmove(ether->netif.addr, ether->ea, Eaddrlen);
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
	error(Ebadctl);
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
		error(Enoifc);
	snprintf(name, sizeof(name), "ether%d.%d", ether->ctlrno, id);
	vlan = ether->vlans[fid];
	if (vlan == NULL) {
		vlan = kzmalloc(sizeof(struct ether), 1);
		if (vlan == NULL)
			error(Enovmem);
		rwinit(&vlan->rwlock);
		qlock_init(&vlan->vlq);
		netifinit(&vlan->netif, name, Ntypes, ether->netif.limit);
		ether->vlans[fid] = vlan;	/* id is still zero, can't be matched */
		ether->nvlan++;
	} else
		memmove(vlan->netif.name, name, KNAMELEN - 1);
	vlan->attach = nop;
	vlan->transmit = NULL;
	vlan->ctl = vlanctl;
	vlan->irq = -1;
	vlan->netif.promiscuous = ether->netif.promiscuous;
	vlan->netif.multicast = ether->netif.multicast;
	vlan->netif.arg = vlan;
	vlan->netif.mbps = ether->netif.mbps;
	vlan->fullduplex = ether->fullduplex;
	vlan->encry = ether->encry;
	vlan->minmtu = ether->minmtu;
	vlan->maxmtu = ether->maxmtu;
	vlan->ctlrno = ether->ctlrno;
	vlan->vlanid = id;
	vlan->netif.alen = Eaddrlen;
	memmove(vlan->netif.addr, ether->netif.addr, sizeof(vlan->netif.addr));
	memmove(vlan->netif.bcast, ether->netif.bcast, sizeof(ether->netif.bcast));
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
	int i, n, ctlrno;
	char name[KNAMELEN], buf[128];

	for (ether = 0, ctlrno = 0; ctlrno < MaxEther; ctlrno++) {
		if (ether == 0)
			ether = kzmalloc(sizeof(struct ether), 0);
		memset(ether, 0, sizeof(struct ether));
		rwinit(&ether->rwlock);
		qlock_init(&ether->vlq);
		ether->ctlrno = ctlrno;
		ether->netif.mbps = 10;
		ether->minmtu = ETHERMINTU;
		ether->maxmtu = ETHERMAXTU;
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
						 ether->type, ether->netif.mbps, ether->port,
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

			if (ether->netif.mbps == 100) {
				netifinit(&ether->netif, name, Ntypes, 256 * 1024);
				if (ether->oq == 0)
					ether->oq = qopen(256 * 1024, Qmsg, 0, 0);
			} else {
				netifinit(&ether->netif, name, Ntypes, 64 * 1024);
				if (ether->oq == 0)
					ether->oq = qopen(64 * 1024, Qmsg, 0, 0);
			}
			if (ether->oq == 0)
				panic("etherreset %s", name);
			ether->netif.alen = Eaddrlen;
			memmove(ether->netif.addr, ether->ea, Eaddrlen);
			memset(ether->netif.bcast, 0xFF, Eaddrlen);

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
	'l',
	"ether",

	etherreset,
	devinit,
	ethershutdown,
	etherattach,
	etherwalk,
	etherstat,
	etheropen,
	devcreate,
	etherclose,
	etherread,
	etherbread,
	etherwrite,
	etherbwrite,
	devremove,
	etherwstat,
	etherpower,
	devchaninfo,
};
