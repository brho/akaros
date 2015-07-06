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

/*
 *  address resolution tables
 */

enum {
	NHASH = (1 << 6),
	NCACHE = 256,

	AOK = 1,
	AWAIT = 2,
};

char *arpstate[] = {
	"UNUSED",
	"OK",
	"WAIT",
};

/*
 *  one per Fs
 */
struct arp {
	qlock_t qlock;
	struct Fs *f;
	struct arpent *hash[NHASH];
	struct arpent cache[NCACHE];
	struct arpent *rxmt;
	struct proc *rxmitp;		/* neib sol re-transmit proc */
	struct rendez rxmtq;
	struct block *dropf, *dropl;
};

char *Ebadarp = "bad arp";

#define haship(s) ((s)[IPaddrlen-1]%NHASH)

int ReTransTimer = RETRANS_TIMER;
static void rxmitproc(void *v);

void arpinit(struct Fs *f)
{
	f->arp = kzmalloc(sizeof(struct arp), KMALLOC_WAIT);
	qlock_init(&f->arp->qlock);
	rendez_init(&f->arp->rxmtq);
	f->arp->f = f;
	f->arp->rxmt = NULL;
	f->arp->dropf = f->arp->dropl = NULL;
	ktask("rxmitproc", rxmitproc, f->arp);
}

/*
 *  create a new arp entry for an ip address.
 */
static struct arpent *newarp6(struct arp *arp, uint8_t * ip, struct Ipifc *ifc,
							  int addrxt)
{
	unsigned int t;
	struct block *next, *xp;
	struct arpent *a, *e, *f, **l;
	struct medium *m = ifc->m;
	int empty;

	/* find oldest entry */
	e = &arp->cache[NCACHE];
	a = arp->cache;
	t = a->utime;
	for (f = a; f < e; f++) {
		if (f->utime < t) {
			t = f->utime;
			a = f;
		}
	}

	/* dump waiting packets */
	xp = a->hold;
	a->hold = NULL;

	if (isv4(a->ip)) {
		while (xp) {
			next = xp->list;
			freeblist(xp);
			xp = next;
		}
	} else {	// queue icmp unreachable for rxmitproc later on, w/o arp lock
		if (xp) {
			if (arp->dropl == NULL)
				arp->dropf = xp;
			else
				arp->dropl->list = xp;

			for (next = xp->list; next; next = next->list)
				xp = next;
			arp->dropl = xp;
			rendez_wakeup(&arp->rxmtq);
		}
	}

	/* take out of current chain */
	l = &arp->hash[haship(a->ip)];
	for (f = *l; f; f = f->hash) {
		if (f == a) {
			*l = a->hash;
			break;
		}
		l = &f->hash;
	}

	/* insert into new chain */
	l = &arp->hash[haship(ip)];
	a->hash = *l;
	*l = a;

	memmove(a->ip, ip, sizeof(a->ip));
	a->utime = NOW;
	a->ctime = 0;	/* somewhat of a "last sent time".  0, to trigger a send. */
	a->type = m;

	a->rtime = NOW + ReTransTimer;
	a->rxtsrem = MAX_MULTICAST_SOLICIT;
	a->ifc = ifc;
	a->ifcid = ifc->ifcid;

	/* put to the end of re-transmit chain; addrxt is 0 when isv4(a->ip) */
	if (!ipismulticast(a->ip) && addrxt) {
		l = &arp->rxmt;
		empty = (*l == NULL);

		for (f = *l; f; f = f->nextrxt) {
			if (f == a) {
				*l = a->nextrxt;
				break;
			}
			l = &f->nextrxt;
		}
		for (f = *l; f; f = f->nextrxt) {
			l = &f->nextrxt;
		}
		*l = a;
		if (empty)
			rendez_wakeup(&arp->rxmtq);
	}

	a->nextrxt = NULL;

	return a;
}

/* called with arp qlocked */

void cleanarpent(struct arp *arp, struct arpent *a)
{
	struct arpent *f, **l;

	a->utime = 0;
	a->ctime = 0;
	a->type = 0;
	a->state = 0;

	/* take out of current chain */
	l = &arp->hash[haship(a->ip)];
	for (f = *l; f; f = f->hash) {
		if (f == a) {
			*l = a->hash;
			break;
		}
		l = &f->hash;
	}

	/* take out of re-transmit chain */
	l = &arp->rxmt;
	for (f = *l; f; f = f->nextrxt) {
		if (f == a) {
			*l = a->nextrxt;
			break;
		}
		l = &f->nextrxt;
	}
	a->nextrxt = NULL;
	a->hash = NULL;
	a->hold = NULL;
	a->last = NULL;
	a->ifc = NULL;
}

/*
 *  fill in the media address if we have it.  Otherwise return an
 *  arpent that represents the state of the address resolution FSM
 *  for ip.  Add the packet to be sent onto the list of packets
 *  waiting for ip->mac to be resolved.
 */
struct arpent *arpget(struct arp *arp, struct block *bp, int version,
					  struct Ipifc *ifc, uint8_t * ip, uint8_t * mac)
{
	int hash, len;
	struct arpent *a;
	struct medium *type = ifc->m;
	uint8_t v6ip[IPaddrlen];
	uint16_t *s, *d;

	if (version == V4) {
		v4tov6(v6ip, ip);
		ip = v6ip;
	}

	qlock(&arp->qlock);
	hash = haship(ip);
	for (a = arp->hash[hash]; a; a = a->hash) {
		if (ipcmp(ip, a->ip) == 0)
			if (type == a->type)
				break;
	}

	if (a == NULL) {
		a = newarp6(arp, ip, ifc, (version != V4));
		a->state = AWAIT;
	}
	a->utime = NOW;
	if (a->state == AWAIT) {
		if (bp != NULL) {
			if (a->hold)
				a->last->list = bp;
			else
				a->hold = bp;
			a->last = bp;
			bp->list = NULL;
		}
		return a;	/* return with arp qlocked */
	}

	s = (uint16_t *)a->mac;
	d = (uint16_t *)mac;
	len = a->type->maclen / 2;
	while (len) {
		*d++ = *s++;
		len--;
	}

	/* remove old entries */
	if (NOW - a->ctime > 15 * 60 * 1000)
		cleanarpent(arp, a);

	qunlock(&arp->qlock);
	return NULL;
}

/*
 * called with arp locked
 */
void arprelease(struct arp *arp, struct arpent *a)
{
	qunlock(&arp->qlock);
}

/*
 * Copy out the mac address from the arpent.  Return the
 * block waiting to get sent to this mac address.
 *
 * called with arp locked
 */
struct block *arpresolve(struct arp *arp, struct arpent *a, struct medium *type,
						 uint8_t * mac)
{
	struct block *bp;
	struct arpent *f, **l;

	if (!isv4(a->ip)) {
		l = &arp->rxmt;
		for (f = *l; f; f = f->nextrxt) {
			if (f == a) {
				*l = a->nextrxt;
				break;
			}
			l = &f->nextrxt;
		}
	}

	memmove(a->mac, mac, type->maclen);
	a->type = type;
	a->state = AOK;
	a->utime = NOW;
	bp = a->hold;
	a->hold = NULL;
	/* brho: it looks like we return the entire hold list, though it might be
	 * purged by now via some other crazy arp list management.  our callers
	 * can't handle the arp's b->list stuff. */
	assert(!bp->list);
	qunlock(&arp->qlock);

	return bp;
}

void
arpenter(struct Fs *fs, int version, uint8_t * ip, uint8_t * mac, int n,
		 int refresh)
{
	ERRSTACK(1);
	struct arp *arp;
	struct route *r;
	struct arpent *a, *f, **l;
	struct Ipifc *ifc;
	struct medium *type;
	struct block *bp, *next;
	uint8_t v6ip[IPaddrlen];

	arp = fs->arp;

	if (n != 6) {
//      print("arp: len = %d\n", n);
		return;
	}

	switch (version) {
		case V4:
			r = v4lookup(fs, ip, NULL);
			v4tov6(v6ip, ip);
			ip = v6ip;
			break;
		case V6:
			r = v6lookup(fs, ip, NULL);
			break;
		default:
			panic("arpenter: version %d", version);
			return;	/* to supress warnings */
	}

	if (r == NULL) {
//      print("arp: no route for entry\n");
		return;
	}

	ifc = r->rt.ifc;
	type = ifc->m;

	qlock(&arp->qlock);
	for (a = arp->hash[haship(ip)]; a; a = a->hash) {
		if (a->type != type || (a->state != AWAIT && a->state != AOK))
			continue;

		if (ipcmp(a->ip, ip) == 0) {
			a->state = AOK;
			memmove(a->mac, mac, type->maclen);

			if (version == V6) {
				/* take out of re-transmit chain */
				l = &arp->rxmt;
				for (f = *l; f; f = f->nextrxt) {
					if (f == a) {
						*l = a->nextrxt;
						break;
					}
					l = &f->nextrxt;
				}
			}

			a->ifc = ifc;
			a->ifcid = ifc->ifcid;
			bp = a->hold;
			a->hold = NULL;
			if (version == V4)
				ip += IPv4off;
			a->utime = NOW;
			a->ctime = a->utime;
			qunlock(&arp->qlock);

			while (bp) {
				next = bp->list;
				if (ifc != NULL) {
					if (waserror()) {
						runlock(&ifc->rwlock);
						nexterror();
					}
					rlock(&ifc->rwlock);
					if (ifc->m != NULL)
						ifc->m->bwrite(ifc, bp, version, ip);
					else
						freeb(bp);
					runlock(&ifc->rwlock);
					poperror();
				} else
					freeb(bp);
				bp = next;
			}
			return;
		}
	}

	if (refresh == 0) {
		a = newarp6(arp, ip, ifc, 0);
		a->state = AOK;
		a->type = type;
		a->ctime = NOW;
		memmove(a->mac, mac, type->maclen);
	}

	qunlock(&arp->qlock);
}

int arpwrite(struct Fs *fs, char *s, int len)
{
	int n;
	struct route *r;
	struct arp *arp;
	struct block *bp;
	struct arpent *a, *fl, **l;
	struct medium *m;
	char *f[4], buf[256];
	uint8_t ip[IPaddrlen], mac[MAClen];

	arp = fs->arp;

	if (len == 0)
		error(Ebadarp);
	if (len >= sizeof(buf))
		len = sizeof(buf) - 1;
	strncpy(buf, s, len);
	buf[len] = 0;
	if (len > 0 && buf[len - 1] == '\n')
		buf[len - 1] = 0;

	n = getfields(buf, f, 4, 1, " ");
	if (strcmp(f[0], "flush") == 0) {
		qlock(&arp->qlock);
		for (a = arp->cache; a < &arp->cache[NCACHE]; a++) {
			memset(a->ip, 0, sizeof(a->ip));
			memset(a->mac, 0, sizeof(a->mac));
			a->hash = NULL;
			a->state = 0;
			a->utime = 0;
			while (a->hold != NULL) {
				bp = a->hold->list;
				freeblist(a->hold);
				a->hold = bp;
			}
		}
		memset(arp->hash, 0, sizeof(arp->hash));
// clear all pkts on these lists (rxmt, dropf/l)
		arp->rxmt = NULL;
		arp->dropf = NULL;
		arp->dropl = NULL;
		qunlock(&arp->qlock);
	} else if (strcmp(f[0], "add") == 0) {
		switch (n) {
			default:
				error(Ebadarg);
			case 3:
				parseip(ip, f[1]);
				if (isv4(ip))
					r = v4lookup(fs, ip + IPv4off, NULL);
				else
					r = v6lookup(fs, ip, NULL);
				if (r == NULL)
					error("Destination unreachable");
				m = r->rt.ifc->m;
				n = parsemac(mac, f[2], m->maclen);
				break;
			case 4:
				m = ipfindmedium(f[1]);
				if (m == NULL)
					error(Ebadarp);
				parseip(ip, f[2]);
				n = parsemac(mac, f[3], m->maclen);
				break;
		}

		if (m->ares == NULL)
			error(Ebadarp);

		m->ares(fs, V6, ip, mac, n, 0);
	} else if (strcmp(f[0], "del") == 0) {
		if (n != 2)
			error(Ebadarg);

		parseip(ip, f[1]);
		qlock(&arp->qlock);

		l = &arp->hash[haship(ip)];
		for (a = *l; a; a = a->hash) {
			if (memcmp(ip, a->ip, sizeof(a->ip)) == 0) {
				*l = a->hash;
				break;
			}
			l = &a->hash;
		}

		if (a) {
			/* take out of re-transmit chain */
			l = &arp->rxmt;
			for (fl = *l; fl; fl = fl->nextrxt) {
				if (fl == a) {
					*l = a->nextrxt;
					break;
				}
				l = &fl->nextrxt;
			}

			a->nextrxt = NULL;
			a->hash = NULL;
			a->hold = NULL;
			a->last = NULL;
			a->ifc = NULL;
			memset(a->ip, 0, sizeof(a->ip));
			memset(a->mac, 0, sizeof(a->mac));
		}
		qunlock(&arp->qlock);
	} else
		error(Ebadarp);

	return len;
}

enum {
	Alinelen = 90,
};

static char *aformat = "%-6.6s %-8.8s %-40.40I %E\n";

int arpread(struct arp *arp, char *p, uint32_t offset, int len)
{
	struct arpent *a;
	int n;
	int left = len;
	int amt;

	if (offset % Alinelen)
		return 0;

	offset = offset / Alinelen;
	len = len / Alinelen;

	n = 0;
	for (a = arp->cache; len > 0 && a < &arp->cache[NCACHE]; a++) {
		if (a->state == 0)
			continue;
		if (offset > 0) {
			offset--;
			continue;
		}
		len--;
		left--;
		qlock(&arp->qlock);
		amt = snprintf(p + n, left, aformat, a->type->name, arpstate[a->state],
		               a->ip, a->mac);
		n += amt;
		left -= amt;
		qunlock(&arp->qlock);
	}

	return n;
}

static uint64_t rxmitsols(struct arp *arp)
{
	unsigned int sflag;
	struct block *next, *xp;
	struct arpent *a, *b, **l;
	struct Fs *f;
	uint8_t ipsrc[IPaddrlen];
	struct Ipifc *ifc = NULL;
	uint64_t nrxt;

	qlock(&arp->qlock);
	f = arp->f;

	a = arp->rxmt;
	if (a == NULL) {
		nrxt = 0;
		goto dodrops;	//return nrxt;
	}
	nrxt = a->rtime - NOW;
	if (nrxt > 3 * ReTransTimer / 4)
		goto dodrops;	//return nrxt;

	for (; a; a = a->nextrxt) {
		ifc = a->ifc;
		assert(ifc != NULL);
		if ((a->rxtsrem <= 0) || !(canrlock(&ifc->rwlock))
			|| (a->ifcid != ifc->ifcid)) {
			xp = a->hold;
			a->hold = NULL;

			if (xp) {
				if (arp->dropl == NULL)
					arp->dropf = xp;
				else
					arp->dropl->list = xp;
			}

			cleanarpent(arp, a);
		} else
			break;
	}
	if (a == NULL)
		goto dodrops;

	qunlock(&arp->qlock);	/* for icmpns */
	if ((sflag = ipv6anylocal(ifc, ipsrc)) != SRC_UNSPEC)
		icmpns(f, ipsrc, sflag, a->ip, TARG_MULTI, ifc->mac);

	runlock(&ifc->rwlock);
	qlock(&arp->qlock);

	/* put to the end of re-transmit chain */
	l = &arp->rxmt;
	for (b = *l; b; b = b->nextrxt) {
		if (b == a) {
			*l = a->nextrxt;
			break;
		}
		l = &b->nextrxt;
	}
	for (b = *l; b; b = b->nextrxt) {
		l = &b->nextrxt;
	}
	*l = a;
	a->rxtsrem--;
	a->nextrxt = NULL;
	a->rtime = NOW + ReTransTimer;

	a = arp->rxmt;
	if (a == NULL)
		nrxt = 0;
	else
		nrxt = a->rtime - NOW;

dodrops:
	xp = arp->dropf;
	arp->dropf = NULL;
	arp->dropl = NULL;
	qunlock(&arp->qlock);

	for (; xp; xp = next) {
		next = xp->list;
		icmphostunr(f, ifc, xp, icmp6_adr_unreach, 1);
	}

	return nrxt;

}

static int rxready(void *v)
{
	struct arp *arp = (struct arp *)v;
	int x;

	x = ((arp->rxmt != NULL) || (arp->dropf != NULL));

	return x;
}

static void rxmitproc(void *v)
{
	ERRSTACK(2);
	struct arp *arp = v;
	uint64_t wakeupat;

	arp->rxmitp = current;
	//print("arp rxmitproc started\n");
	if (waserror()) {
		arp->rxmitp = 0;
		poperror();
		warn("arp rxmit ktask exited");
		return;
	}
	for (;;) {
		wakeupat = rxmitsols(arp);
		if (wakeupat == 0)
			rendez_sleep(&arp->rxmtq, rxready, v);
		else if (wakeupat > ReTransTimer / 4)
			udelay_sched(wakeupat * 1000);
	}
	poperror();
}
