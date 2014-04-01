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

enum {
	Qtopdir = 1,				/* top level directory */
	Qtopbase,
	Qarp = Qtopbase,
	Qbootp,
	Qndb,
	Qiproute,
	Qiprouter,
	Qipselftab,
	Qlog,

	Qprotodir,	/* directory for a protocol */
	Qprotobase,
	Qclone = Qprotobase,
	Qstats,

	Qconvdir,	/* directory for a conversation */
	Qconvbase,
	Qctl = Qconvbase,
	Qdata,
	Qerr,
	Qlisten,
	Qlocal,
	Qremote,
	Qstatus,
	Qsnoop,

	Logtype = 5,
	Masktype = (1 << Logtype) - 1,
	Logconv = 12,
	Maskconv = (1 << Logconv) - 1,
	Shiftconv = Logtype,
	Logproto = 8,
	Maskproto = (1 << Logproto) - 1,
	Shiftproto = Logtype + Logconv,

	Nfs = 32,
};
#define TYPE(x) 	( ((uint32_t)(x).path) & Masktype )
#define CONV(x) 	( (((uint32_t)(x).path) >> Shiftconv) & Maskconv )
#define PROTO(x) 	( (((uint32_t)(x).path) >> Shiftproto) & Maskproto )
#define QID(p, c, y) 	( ((p)<<(Shiftproto)) | ((c)<<Shiftconv) | (y))
static char network[] = "network";

qlock_t fslock;
struct Fs *ipfs[Nfs];			/* attached fs's */
struct queue *qlog;

extern void nullmediumlink(void);
extern void pktmediumlink(void);
extern char *eve;
static long ndbwrite(struct Fs *, char *unused_char_p_t, uint32_t, int);
static void closeconv(struct conv *);

static int ip3gen(struct chan *c, int i, struct dir *dp)
{
	struct qid q;
	struct conv *cv;
	char *p;

	cv = ipfs[c->dev]->p[PROTO(c->qid)]->conv[CONV(c->qid)];
	if (cv->owner == NULL)
		kstrdup(&cv->owner, eve);
	mkqid(&q, QID(PROTO(c->qid), CONV(c->qid), i), 0, QTFILE);

	switch (i) {
		default:
			return -1;
		case Qctl:
			devdir(c, q, "ctl", 0, cv->owner, cv->perm, dp);
			return 1;
		case Qdata:
			devdir(c, q, "data", qlen(cv->rq), cv->owner, cv->perm, dp);
			return 1;
		case Qerr:
			devdir(c, q, "err", qlen(cv->eq), cv->owner, cv->perm, dp);
			return 1;
		case Qlisten:
			devdir(c, q, "listen", 0, cv->owner, cv->perm, dp);
			return 1;
		case Qlocal:
			p = "local";
			break;
		case Qremote:
			p = "remote";
			break;
		case Qsnoop:
			if (strcmp(cv->p->name, "ipifc") != 0)
				return -1;
			devdir(c, q, "snoop", qlen(cv->sq), cv->owner, 0400, dp);
			return 1;
		case Qstatus:
			p = "status";
			break;
	}
	devdir(c, q, p, 0, cv->owner, 0444, dp);
	return 1;
}

static int ip2gen(struct chan *c, int i, struct dir *dp)
{
	struct qid q;

	switch (i) {
		case Qclone:
			mkqid(&q, QID(PROTO(c->qid), 0, Qclone), 0, QTFILE);
			devdir(c, q, "clone", 0, network, 0666, dp);
			return 1;
		case Qstats:
			mkqid(&q, QID(PROTO(c->qid), 0, Qstats), 0, QTFILE);
			devdir(c, q, "stats", 0, network, 0444, dp);
			return 1;
	}
	return -1;
}

static int ip1gen(struct chan *c, int i, struct dir *dp)
{
	struct qid q;
	char *p;
	int prot;
	int len = 0;
	struct Fs *f;
	extern uint32_t kerndate;

	f = ipfs[c->dev];

	prot = 0666;
	mkqid(&q, QID(0, 0, i), 0, QTFILE);
	switch (i) {
		default:
			return -1;
		case Qarp:
			p = "arp";
			break;
		case Qbootp:
			p = "bootp";
			if (bootp == NULL)
				return 0;
			break;
		case Qndb:
			p = "ndb";
			len = strlen(f->ndb);
			q.vers = f->ndbvers;
			break;
		case Qiproute:
			p = "iproute";
			break;
		case Qipselftab:
			p = "ipselftab";
			prot = 0444;
			break;
		case Qiprouter:
			p = "iprouter";
			break;
		case Qlog:
			p = "log";
			break;
	}
	devdir(c, q, p, len, network, prot, dp);
	if (i == Qndb && f->ndbmtime > kerndate)
		dp->mtime = f->ndbmtime;
	return 1;
}

static int
ipgen(struct chan *c, char *unused_char_p_t, struct dirtab *d, int unused_int,
	  int s, struct dir *dp)
{
	struct qid q;
	struct conv *cv;
	struct Fs *f;

	f = ipfs[c->dev];

	switch (TYPE(c->qid)) {
		case Qtopdir:
			if (s == DEVDOTDOT) {
				mkqid(&q, QID(0, 0, Qtopdir), 0, QTDIR);
				snprintf(get_cur_genbuf(), GENBUF_SZ, "#I%lu", c->dev);
				devdir(c, q, get_cur_genbuf(), 0, network, 0555, dp);
				return 1;
			}
			if (s < f->np) {
				if (f->p[s]->connect == NULL)
					return 0;	/* protocol with no user interface */
				mkqid(&q, QID(s, 0, Qprotodir), 0, QTDIR);
				devdir(c, q, f->p[s]->name, 0, network, 0555, dp);
				return 1;
			}
			s -= f->np;
			return ip1gen(c, s + Qtopbase, dp);
		case Qarp:
		case Qbootp:
		case Qndb:
		case Qlog:
		case Qiproute:
		case Qiprouter:
		case Qipselftab:
			return ip1gen(c, TYPE(c->qid), dp);
		case Qprotodir:
			if (s == DEVDOTDOT) {
				mkqid(&q, QID(0, 0, Qtopdir), 0, QTDIR);
				snprintf(get_cur_genbuf(), GENBUF_SZ, "#I%lu", c->dev);
				devdir(c, q, get_cur_genbuf(), 0, network, 0555, dp);
				return 1;
			}
			if (s < f->p[PROTO(c->qid)]->ac) {
				cv = f->p[PROTO(c->qid)]->conv[s];
				snprintf(get_cur_genbuf(), GENBUF_SZ, "%d", s);
				mkqid(&q, QID(PROTO(c->qid), s, Qconvdir), 0, QTDIR);
				devdir(c, q, get_cur_genbuf(), 0, cv->owner, 0555, dp);
				return 1;
			}
			s -= f->p[PROTO(c->qid)]->ac;
			return ip2gen(c, s + Qprotobase, dp);
		case Qclone:
		case Qstats:
			return ip2gen(c, TYPE(c->qid), dp);
		case Qconvdir:
			if (s == DEVDOTDOT) {
				s = PROTO(c->qid);
				mkqid(&q, QID(s, 0, Qprotodir), 0, QTDIR);
				devdir(c, q, f->p[s]->name, 0, network, 0555, dp);
				return 1;
			}
			return ip3gen(c, s + Qconvbase, dp);
		case Qctl:
		case Qdata:
		case Qerr:
		case Qlisten:
		case Qlocal:
		case Qremote:
		case Qstatus:
		case Qsnoop:
			return ip3gen(c, TYPE(c->qid), dp);
	}
	return -1;
}

static void ipinit(void)
{
	qlock_init(&fslock);
	nullmediumlink();
	pktmediumlink();
/* if only
	fmtinstall('i', eipfmt);
	fmtinstall('I', eipfmt);
	fmtinstall('E', eipfmt);
	fmtinstall('V', eipfmt);
	fmtinstall('M', eipfmt);
*/
}

static void ipreset(void)
{
}

static struct Fs *ipgetfs(int dev)
{
	extern void (*ipprotoinit[]) (struct Fs *);
	struct Fs *f;
	int i;

	if (dev >= Nfs)
		return NULL;

	qlock(&fslock);
	if (ipfs[dev] == NULL) {
		f = kzmalloc(sizeof(struct Fs), KMALLOC_WAIT);
		rwinit(&f->rwlock);
		qlock_init(&f->iprouter.qlock);
		ip_init(f);
		arpinit(f);
		netloginit(f);
		for (i = 0; ipprotoinit[i]; i++)
			ipprotoinit[i] (f);
		f->dev = dev;
		ipfs[dev] = f;
	}
	qunlock(&fslock);

	return ipfs[dev];
}

struct IPaux *newipaux(char *owner, char *tag)
{
	struct IPaux *a;
	int n;

	a = kzmalloc(sizeof(*a), 0);
	kstrdup(&a->owner, owner);
	memset(a->tag, ' ', sizeof(a->tag));
	n = strlen(tag);
	if (n > sizeof(a->tag))
		n = sizeof(a->tag);
	memmove(a->tag, tag, n);
	return a;
}

#define ATTACHER(c) (((struct IPaux*)((c)->aux))->owner)

static struct chan *ipattach(char *spec)
{
	struct chan *c;
	int dev;

	dev = atoi(spec);
	if (dev >= Nfs)
		error("bad specification");

	ipgetfs(dev);
	c = devattach('I', spec);
	mkqid(&c->qid, QID(0, 0, Qtopdir), 0, QTDIR);
	c->dev = dev;

	c->aux = newipaux(commonuser(), "none");

	return c;
}

static struct walkqid *ipwalk(struct chan *c, struct chan *nc, char **name,
							  int nname)
{
	struct IPaux *a = c->aux;
	struct walkqid *w;

	w = devwalk(c, nc, name, nname, NULL, 0, ipgen);
	if (w != NULL && w->clone != NULL)
		w->clone->aux = newipaux(a->owner, a->tag);
	return w;
}

static int ipstat(struct chan *c, uint8_t * db, int n)
{
	return devstat(c, db, n, NULL, 0, ipgen);
}

static int should_wake(void *arg)
{
	struct conv *cv = arg;
	/* signal that the conv is closed */
	if (qisclosed(cv->rq))
		return TRUE;
	return cv->incall != NULL;
}

static int m2p[] = {
	[OREAD] 4,
	[OWRITE] 2,
	[ORDWR] 6
};

static struct chan *ipopen(struct chan *c, int omode)
{
	ERRSTACK(2);
	struct conv *cv, *nc;
	struct Proto *p;
	int perm;
	struct Fs *f;

	perm = m2p[omode & 3];

	f = ipfs[c->dev];

	switch (TYPE(c->qid)) {
		default:
			break;
		case Qndb:
			if (omode & (OWRITE | OTRUNC) && !iseve())
				error(Eperm);
			if ((omode & (OWRITE | OTRUNC)) == (OWRITE | OTRUNC))
				f->ndb[0] = 0;
			break;
		case Qlog:
			netlogopen(f);
			break;
		case Qiprouter:
			iprouteropen(f);
			break;
		case Qiproute:
			break;
		case Qtopdir:
		case Qprotodir:
		case Qconvdir:
		case Qstatus:
		case Qremote:
		case Qlocal:
		case Qstats:
		case Qbootp:
		case Qipselftab:
			if (!IS_RDONLY(omode))
				error(Eperm);
			break;
		case Qsnoop:
			if (!IS_RDONLY(omode))
				error(Eperm);
			p = f->p[PROTO(c->qid)];
			cv = p->conv[CONV(c->qid)];
			if (strcmp(ATTACHER(c), cv->owner) != 0 && !iseve())
				error(Eperm);
			atomic_inc(&cv->snoopers);
			break;
		case Qclone:
			p = f->p[PROTO(c->qid)];
			qlock(&p->qlock);
			if (waserror()) {
				qunlock(&p->qlock);
				nexterror();
			}
			cv = Fsprotoclone(p, ATTACHER(c));
			qunlock(&p->qlock);
			poperror();
			if (cv == NULL) {
				error(Enodev);
				break;
			}
			mkqid(&c->qid, QID(p->x, cv->x, Qctl), 0, QTFILE);
			break;
		case Qdata:
		case Qctl:
		case Qerr:
			p = f->p[PROTO(c->qid)];
			qlock(&p->qlock);
			cv = p->conv[CONV(c->qid)];
			qlock(&cv->qlock);
			if (waserror()) {
				qunlock(&cv->qlock);
				qunlock(&p->qlock);
				nexterror();
			}
			if ((perm & (cv->perm >> 6)) != perm) {
				if (strcmp(ATTACHER(c), cv->owner) != 0)
					error(Eperm);
				if ((perm & cv->perm) != perm)
					error(Eperm);

			}
			cv->inuse++;
			if (cv->inuse == 1) {
				kstrdup(&cv->owner, ATTACHER(c));
				cv->perm = 0660;
			}
			qunlock(&cv->qlock);
			qunlock(&p->qlock);
			poperror();
			break;
		case Qlisten:
			cv = f->p[PROTO(c->qid)]->conv[CONV(c->qid)];
			if ((perm & (cv->perm >> 6)) != perm) {
				if (strcmp(ATTACHER(c), cv->owner) != 0)
					error(Eperm);
				if ((perm & cv->perm) != perm)
					error(Eperm);

			}

			if (cv->state != Announced)
				error("not announced");

			if (waserror()) {
				closeconv(cv);
				nexterror();
			}
			qlock(&cv->qlock);
			cv->inuse++;
			qunlock(&cv->qlock);

			nc = NULL;
			while (nc == NULL) {
				/* give up if we got a hangup */
				if (qisclosed(cv->rq))
					error("listen hungup");

				qlock(&cv->listenq);
				if (waserror()) {
					qunlock(&cv->listenq);
					nexterror();
				}

				/* wait for a connect */
				rendez_sleep(&cv->listenr, should_wake, cv);

				/* if there is a concurrent hangup, they will hold the qlock
				 * until the hangup is complete, including closing the cv->rq */
				qlock(&cv->qlock);
				nc = cv->incall;
				if (nc != NULL) {
					cv->incall = nc->next;
					mkqid(&c->qid, QID(PROTO(c->qid), nc->x, Qctl), 0, QTFILE);
					kstrdup(&cv->owner, ATTACHER(c));
				}
				qunlock(&cv->qlock);

				qunlock(&cv->listenq);
				poperror();
			}
			closeconv(cv);
			poperror();
			break;
	}
	c->mode = openmode(omode);
	c->flag |= COPEN;
	c->offset = 0;
	return c;
}

static int ipwstat(struct chan *c, uint8_t * dp, int n)
{
	ERRSTACK(2);
	struct dir *d;
	struct conv *cv;
	struct Fs *f;
	struct Proto *p;

	f = ipfs[c->dev];
	switch (TYPE(c->qid)) {
		default:
			error(Eperm);
			break;
		case Qctl:
		case Qdata:
			break;
	}

	d = kzmalloc(sizeof(*d) + n, 0);
	if (waserror()) {
		kfree(d);
		nexterror();
	}
	n = convM2D(dp, n, d, (char *)&d[1]);
	if (n == 0)
		error(Eshortstat);
	p = f->p[PROTO(c->qid)];
	cv = p->conv[CONV(c->qid)];
	if (!iseve() && strcmp(ATTACHER(c), cv->owner) != 0)
		error(Eperm);
	if (!emptystr(d->uid))
		kstrdup(&cv->owner, d->uid);
	if (d->mode != ~0UL)
		cv->perm = d->mode & 0777;
	poperror();
	kfree(d);
	return n;
}

/* Should be able to handle any file type chan. Feel free to extend it. */
static char *ipchaninfo(struct chan *ch, char *ret, size_t ret_l)
{
	struct conv *conv;
	struct Proto *proto;
	char *p;
	struct Fs *f;

	f = ipfs[ch->dev];

	switch (TYPE(ch->qid)) {
		default:
			ret = "Unknown type";
			break;
		case Qdata:
			proto = f->p[PROTO(ch->qid)];
			conv = proto->conv[CONV(ch->qid)];
			snprintf(ret, ret_l, "Qdata, proto %s, conv idx %d", proto->name,
					 conv->x);
			break;
		case Qarp:
			ret = "Qarp";
			break;
		case Qiproute:
			ret = "Qiproute";
			break;
		case Qlog:
			ret = "Qlog";
			break;
		case Qndb:
			ret = "Qndb";
			break;
		case Qctl:
			proto = f->p[PROTO(ch->qid)];
			conv = proto->conv[CONV(ch->qid)];
			snprintf(ret, ret_l, "Qctl, proto %s, conv idx %d", proto->name,
					 conv->x);
			break;
	}
	return ret;
}

static void closeconv(struct conv *cv)
{
	struct conv *nc;
	struct Ipmulti *mp;

	qlock(&cv->qlock);

	if (--cv->inuse > 0) {
		qunlock(&cv->qlock);
		return;
	}

	/* close all incoming calls since no listen will ever happen */
	for (nc = cv->incall; nc; nc = cv->incall) {
		cv->incall = nc->next;
		closeconv(nc);
	}
	cv->incall = NULL;

	kstrdup(&cv->owner, network);
	cv->perm = 0660;

	while ((mp = cv->multi) != NULL)
		ipifcremmulti(cv, mp->ma, mp->ia);

	cv->r = NULL;
	cv->rgen = 0;
	cv->p->close(cv);
	cv->state = Idle;
	qunlock(&cv->qlock);
}

static void ipclose(struct chan *c)
{
	struct Fs *f;

	f = ipfs[c->dev];
	switch (TYPE(c->qid)) {
		default:
			break;
		case Qlog:
			if (c->flag & COPEN)
				netlogclose(f);
			break;
		case Qiprouter:
			if (c->flag & COPEN)
				iprouterclose(f);
			break;
		case Qdata:
		case Qctl:
		case Qerr:
			if (c->flag & COPEN)
				closeconv(f->p[PROTO(c->qid)]->conv[CONV(c->qid)]);
			break;
		case Qsnoop:
			if (c->flag & COPEN)
				atomic_dec(&f->p[PROTO(c->qid)]->conv[CONV(c->qid)]->snoopers);
			break;
	}
	kfree(((struct IPaux *)c->aux)->owner);
	kfree(c->aux);
}

enum {
	Statelen = 32 * 1024,
};

static long ipread(struct chan *ch, void *a, long n, int64_t off)
{
	struct conv *c;
	struct Proto *x;
	char *buf, *p;
	long rv;
	struct Fs *f;
	uint32_t offset = off;

	f = ipfs[ch->dev];

	p = a;
	switch (TYPE(ch->qid)) {
		default:
			error(Eperm);
		case Qtopdir:
		case Qprotodir:
		case Qconvdir:
			return devdirread(ch, a, n, 0, 0, ipgen);
		case Qarp:
			return arpread(f->arp, a, offset, n);
		case Qbootp:
			return bootpread(a, offset, n);
		case Qndb:
			return readstr(offset, a, n, f->ndb);
		case Qiproute:
			return routeread(f, a, offset, n);
		case Qiprouter:
			return iprouterread(f, a, n);
		case Qipselftab:
			return ipselftabread(f, a, offset, n);
		case Qlog:
			return netlogread(f, a, offset, n);
		case Qctl:
			snprintf(get_cur_genbuf(), GENBUF_SZ, "%lu", CONV(ch->qid));
			return readstr(offset, p, n, get_cur_genbuf());
		case Qremote:
			buf = kzmalloc(Statelen, 0);
			x = f->p[PROTO(ch->qid)];
			c = x->conv[CONV(ch->qid)];
			if (x->remote == NULL) {
				snprintf(buf, Statelen, "%I!%d\n", c->raddr, c->rport);
			} else {
				(*x->remote) (c, buf, Statelen - 2);
			}
			rv = readstr(offset, p, n, buf);
			kfree(buf);
			return rv;
		case Qlocal:
			buf = kzmalloc(Statelen, 0);
			x = f->p[PROTO(ch->qid)];
			c = x->conv[CONV(ch->qid)];
			if (x->local == NULL) {
				snprintf(buf, Statelen, "%I!%d\n", c->laddr, c->lport);
			} else {
				(*x->local) (c, buf, Statelen - 2);
			}
			rv = readstr(offset, p, n, buf);
			kfree(buf);
			return rv;
		case Qstatus:
			buf = kzmalloc(Statelen, 0);
			x = f->p[PROTO(ch->qid)];
			c = x->conv[CONV(ch->qid)];
			(*x->state) (c, buf, Statelen - 2);
			rv = readstr(offset, p, n, buf);
			kfree(buf);
			return rv;
		case Qdata:
			c = f->p[PROTO(ch->qid)]->conv[CONV(ch->qid)];
			return qread(c->rq, a, n);
		case Qerr:
			c = f->p[PROTO(ch->qid)]->conv[CONV(ch->qid)];
			return qread(c->eq, a, n);
		case Qsnoop:
			c = f->p[PROTO(ch->qid)]->conv[CONV(ch->qid)];
			return qread(c->sq, a, n);
		case Qstats:
			x = f->p[PROTO(ch->qid)];
			if (x->stats == NULL)
				error("stats not implemented");
			buf = kzmalloc(Statelen, 0);
			(*x->stats) (x, buf, Statelen);
			rv = readstr(offset, p, n, buf);
			kfree(buf);
			return rv;
	}
}

static struct block *ipbread(struct chan *ch, long n, uint32_t offset)
{
	struct conv *c;
	struct Proto *x;
	struct Fs *f;

	switch (TYPE(ch->qid)) {
		case Qdata:
			f = ipfs[ch->dev];
			x = f->p[PROTO(ch->qid)];
			c = x->conv[CONV(ch->qid)];
			return qbread(c->rq, n);
		default:
			return devbread(ch, n, offset);
	}
}

/*
 *  set local address to be that of the ifc closest to remote address
 */
static void setladdr(struct conv *c)
{
	findlocalip(c->p->f, c->laddr, c->raddr);
}

/*
 *  set a local port making sure the quad of raddr,rport,laddr,lport is unique
 */
static char *setluniqueport(struct conv *c, int lport)
{
	struct Proto *p;
	struct conv *xp;
	int x;

	p = c->p;

	qlock(&p->qlock);
	for (x = 0; x < p->nc; x++) {
		xp = p->conv[x];
		if (xp == NULL)
			break;
		if (xp == c)
			continue;
		if ((xp->state == Connected || xp->state == Announced)
			&& xp->lport == lport
			&& xp->rport == c->rport
			&& ipcmp(xp->raddr, c->raddr) == 0
			&& ipcmp(xp->laddr, c->laddr) == 0) {
			qunlock(&p->qlock);
			return "address in use";
		}
	}
	c->lport = lport;
	qunlock(&p->qlock);
	return NULL;
}

/*
 *  pick a local port and set it
 */
static void setlport(struct conv *c)
{
	struct Proto *p;
	uint16_t *pp;
	int x, found;

	p = c->p;
	if (c->restricted)
		pp = &p->nextrport;
	else
		pp = &p->nextport;
	qlock(&p->qlock);
	for (;; (*pp)++) {
		/*
		 * Fsproto initialises p->nextport to 0 and the restricted
		 * ports (p->nextrport) to 600.
		 * Restricted ports must lie between 600 and 1024.
		 * For the initial condition or if the unrestricted port number
		 * has wrapped round, select a random port between 5000 and 1<<15
		 * to start at.
		 */
		if (c->restricted) {
			if (*pp >= 1024)
				*pp = 600;
		} else
			while (*pp < 5000)
				*pp = nrand(1 << 15);

		found = 0;
		for (x = 0; x < p->nc; x++) {
			if (p->conv[x] == NULL)
				break;
			if (p->conv[x]->lport == *pp) {
				found = 1;
				break;
			}
		}
		if (!found)
			break;
	}
	c->lport = (*pp)++;
	qunlock(&p->qlock);
}

/*
 *  set a local address and port from a string of the form
 *	[address!]port[!r]
 */
static char *setladdrport(struct conv *c, char *str, int announcing)
{
	char *p;
	char *rv;
	uint16_t lport;
	uint8_t addr[IPaddrlen];

	rv = NULL;

	/*
	 *  ignore restricted part if it exists.  it's
	 *  meaningless on local ports.
	 */
	p = strchr(str, '!');
	if (p != NULL) {
		*p++ = 0;
		if (strcmp(p, "r") == 0)
			p = NULL;
	}

	c->lport = 0;
	if (p == NULL) {
		if (announcing)
			ipmove(c->laddr, IPnoaddr);
		else
			setladdr(c);
		p = str;
	} else {
		if (strcmp(str, "*") == 0)
			ipmove(c->laddr, IPnoaddr);
		else {
			parseip(addr, str);
			if (ipforme(c->p->f, addr))
				ipmove(c->laddr, addr);
			else
				return "not a local IP address";
		}
	}

	/* one process can get all connections */
	if (announcing && strcmp(p, "*") == 0) {
		if (!iseve())
			error(Eperm);
		return setluniqueport(c, 0);
	}

	lport = atoi(p);
	if (lport <= 0)
		setlport(c);
	else
		rv = setluniqueport(c, lport);
	return rv;
}

static char *setraddrport(struct conv *c, char *str)
{
	char *p;

	p = strchr(str, '!');
	if (p == NULL)
		return "malformed address";
	*p++ = 0;
	parseip(c->raddr, str);
	c->rport = atoi(p);
	p = strchr(p, '!');
	if (p) {
		if (strstr(p, "!r") != NULL)
			c->restricted = 1;
	}
	return NULL;
}

/*
 *  called by protocol connect routine to set addresses
 */
char *Fsstdconnect(struct conv *c, char *argv[], int argc)
{
	char *p;

	switch (argc) {
		default:
			return "bad args to connect";
		case 2:
			p = setraddrport(c, argv[1]);
			if (p != NULL)
				return p;
			setladdr(c);
			setlport(c);
			break;
		case 3:
			p = setraddrport(c, argv[1]);
			if (p != NULL)
				return p;
			p = setladdrport(c, argv[2], 0);
			if (p != NULL)
				return p;
	}

	if ((memcmp(c->raddr, v4prefix, IPv4off) == 0 &&
		 memcmp(c->laddr, v4prefix, IPv4off) == 0)
		|| ipcmp(c->raddr, IPnoaddr) == 0)
		c->ipversion = V4;
	else
		c->ipversion = V6;

	return NULL;
}

/*
 *  initiate connection and sleep till its set up
 */
static int connected(void *a)
{
	return ((struct conv *)a)->state == Connected;
}

static void connectctlmsg(struct Proto *x, struct conv *c, struct cmdbuf *cb)
{
	ERRSTACK(1);
	char *p;

	if (c->state != 0)
		error(Econinuse);
	c->state = Connecting;
	c->cerr[0] = '\0';
	if (x->connect == NULL)
		error("connect not supported");
	p = x->connect(c, cb->f, cb->nf);
	if (p != NULL)
		error(p);

	qunlock(&c->qlock);
	if (waserror()) {
		qlock(&c->qlock);
		nexterror();
	}
	rendez_sleep(&c->cr, connected, c);
	qlock(&c->qlock);
	poperror();

	if (c->cerr[0] != '\0')
		error(c->cerr);
}

/*
 *  called by protocol announce routine to set addresses
 */
char *Fsstdannounce(struct conv *c, char *argv[], int argc)
{
	memset(c->raddr, 0, sizeof(c->raddr));
	c->rport = 0;
	switch (argc) {
		default:
			return "bad args to announce";
		case 2:
			return setladdrport(c, argv[1], 1);
	}
}

/*
 *  initiate announcement and sleep till its set up
 */
static int announced(void *a)
{
	return ((struct conv *)a)->state == Announced;
}

static void announcectlmsg(struct Proto *x, struct conv *c, struct cmdbuf *cb)
{
	ERRSTACK(1);
	char *p;

	if (c->state != 0)
		error(Econinuse);
	c->state = Announcing;
	c->cerr[0] = '\0';
	if (x->announce == NULL)
		error("announce not supported");
	p = x->announce(c, cb->f, cb->nf);
	if (p != NULL)
		error(p);

	qunlock(&c->qlock);
	if (waserror()) {
		qlock(&c->qlock);
		nexterror();
	}
	rendez_sleep(&c->cr, announced, c);
	qlock(&c->qlock);
	poperror();

	if (c->cerr[0] != '\0')
		error(c->cerr);
}

/*
 *  called by protocol bind routine to set addresses
 */
char *Fsstdbind(struct conv *c, char *argv[], int argc)
{
	switch (argc) {
		default:
			return "bad args to bind";
		case 2:
			return setladdrport(c, argv[1], 0);
	}
}

static void bindctlmsg(struct Proto *x, struct conv *c, struct cmdbuf *cb)
{
	char *p;

	if (x->bind == NULL)
		p = Fsstdbind(c, cb->f, cb->nf);
	else
		p = x->bind(c, cb->f, cb->nf);
	if (p != NULL)
		error(p);
}

static void tosctlmsg(struct conv *c, struct cmdbuf *cb)
{
	if (cb->nf < 2)
		c->tos = 0;
	else
		c->tos = atoi(cb->f[1]);
}

static void ttlctlmsg(struct conv *c, struct cmdbuf *cb)
{
	if (cb->nf < 2)
		c->ttl = MAXTTL;
	else
		c->ttl = atoi(cb->f[1]);
}

static long ipwrite(struct chan *ch, void *v, long n, int64_t off)
{
	ERRSTACK(1);
	struct conv *c;
	struct Proto *x;
	char *p;
	struct cmdbuf *cb;
	uint8_t ia[IPaddrlen], ma[IPaddrlen];
	struct Fs *f;
	char *a;

	a = v;
	f = ipfs[ch->dev];

	switch (TYPE(ch->qid)) {
		default:
			error(Eperm);
		case Qdata:
			x = f->p[PROTO(ch->qid)];
			c = x->conv[CONV(ch->qid)];

			if (c->wq == NULL)
				error(Eperm);

			qwrite(c->wq, a, n);
			break;
		case Qarp:
			return arpwrite(f, a, n);
		case Qiproute:
			return routewrite(f, ch, a, n);
		case Qlog:
			netlogctl(f, a, n);
			return n;
		case Qndb:
			return ndbwrite(f, a, off, n);
		case Qctl:
			x = f->p[PROTO(ch->qid)];
			c = x->conv[CONV(ch->qid)];
			cb = parsecmd(a, n);

			qlock(&c->qlock);
			if (waserror()) {
				qunlock(&c->qlock);
				kfree(cb);
				nexterror();
			}
			if (cb->nf < 1)
				error("short control request");
			if (strcmp(cb->f[0], "connect") == 0)
				connectctlmsg(x, c, cb);
			else if (strcmp(cb->f[0], "announce") == 0)
				announcectlmsg(x, c, cb);
			else if (strcmp(cb->f[0], "bind") == 0)
				bindctlmsg(x, c, cb);
			else if (strcmp(cb->f[0], "ttl") == 0)
				ttlctlmsg(c, cb);
			else if (strcmp(cb->f[0], "tos") == 0)
				tosctlmsg(c, cb);
			else if (strcmp(cb->f[0], "ignoreadvice") == 0)
				c->ignoreadvice = 1;
			else if (strcmp(cb->f[0], "addmulti") == 0) {
				if (cb->nf < 2)
					error("addmulti needs interface address");
				if (cb->nf == 2) {
					if (!ipismulticast(c->raddr))
						error("addmulti for a non multicast address");
					parseip(ia, cb->f[1]);
					ipifcaddmulti(c, c->raddr, ia);
				} else {
					parseip(ma, cb->f[2]);
					if (!ipismulticast(ma))
						error("addmulti for a non multicast address");
					parseip(ia, cb->f[1]);
					ipifcaddmulti(c, ma, ia);
				}
			} else if (strcmp(cb->f[0], "remmulti") == 0) {
				if (cb->nf < 2)
					error("remmulti needs interface address");
				if (!ipismulticast(c->raddr))
					error("remmulti for a non multicast address");
				parseip(ia, cb->f[1]);
				ipifcremmulti(c, c->raddr, ia);
			} else if (x->ctl != NULL) {
				p = x->ctl(c, cb->f, cb->nf);
				if (p != NULL)
					error(p);
			} else
				error("unknown control request");
			qunlock(&c->qlock);
			kfree(cb);
			poperror();
	}
	return n;
}

static long ipbwrite(struct chan *ch, struct block *bp, uint32_t offset)
{
	struct conv *c;
	struct Proto *x;
	struct Fs *f;
	int n;

	switch (TYPE(ch->qid)) {
		case Qdata:
			f = ipfs[ch->dev];
			x = f->p[PROTO(ch->qid)];
			c = x->conv[CONV(ch->qid)];

			if (c->wq == NULL)
				error(Eperm);

			if (bp->next)
				bp = concatblock(bp);
			n = BLEN(bp);
			qbwrite(c->wq, bp);
			return n;
		default:
			return devbwrite(ch, bp, offset);
	}
}

struct dev ipdevtab __devtab = {
	'I',
	"ip",

	ipreset,
	ipinit,
	devshutdown,
	ipattach,
	ipwalk,
	ipstat,
	ipopen,
	devcreate,
	ipclose,
	ipread,
	ipbread,
	ipwrite,
	ipbwrite,
	devremove,
	ipwstat,
	devpower,
	ipchaninfo,
};

int Fsproto(struct Fs *f, struct Proto *p)
{
	if (f->np >= Maxproto)
		return -1;

	qlock_init(&p->qlock);
	p->f = f;

	if (p->ipproto > 0) {
		if (f->t2p[p->ipproto] != NULL)
			return -1;
		f->t2p[p->ipproto] = p;
	}

	p->qid.type = QTDIR;
	p->qid.path = QID(f->np, 0, Qprotodir);
	p->conv = kzmalloc(sizeof(struct conv *) * (p->nc + 1), 0);
	if (p->conv == NULL)
		panic("Fsproto");

	p->x = f->np;
	p->nextport = 0;
	p->nextrport = 600;
	f->p[f->np++] = p;

	return 0;
}

/*
 *  return true if this protocol is
 *  built in
 */
int Fsbuiltinproto(struct Fs *f, uint8_t proto)
{
	return f->t2p[proto] != NULL;
}

/*
 *  called with protocol locked
 */
struct conv *Fsprotoclone(struct Proto *p, char *user)
{
	struct conv *c, **pp, **ep;

retry:
	c = NULL;
	ep = &p->conv[p->nc];
	for (pp = p->conv; pp < ep; pp++) {
		c = *pp;
		if (c == NULL) {
			c = kzmalloc(sizeof(struct conv), 0);
			if (c == NULL)
				error(Enomem);
			qlock_init(&c->qlock);
			qlock_init(&c->listenq);
			rendez_init(&c->cr);
			rendez_init(&c->listenr);
			qlock(&c->qlock);
			c->p = p;
			c->x = pp - p->conv;
			if (p->ptclsize != 0) {
				c->ptcl = kzmalloc(p->ptclsize, 0);
				if (c->ptcl == NULL) {
					kfree(c);
					error(Enomem);
				}
			}
			*pp = c;
			p->ac++;
			c->eq = qopen(1024, Qmsg, 0, 0);
			(*p->create) (c);
			break;
		}
		if (canqlock(&c->qlock)) {
			/*
			 *  make sure both processes and protocol
			 *  are done with this Conv
			 */
			if (c->inuse == 0 && (p->inuse == NULL || (*p->inuse) (c) == 0))
				break;

			qunlock(&c->qlock);
		}
	}
	if (pp >= ep) {
		if (p->gc != NULL && (*p->gc) (p))
			goto retry;
		return NULL;
	}

	c->inuse = 1;
	kstrdup(&c->owner, user);
	c->perm = 0660;
	c->state = Idle;
	ipmove(c->laddr, IPnoaddr);
	ipmove(c->raddr, IPnoaddr);
	c->r = NULL;
	c->rgen = 0;
	c->lport = 0;
	c->rport = 0;
	c->restricted = 0;
	c->ttl = MAXTTL;
	c->tos = DFLTTOS;
	qreopen(c->rq);
	qreopen(c->wq);
	qreopen(c->eq);

	qunlock(&c->qlock);
	return c;
}

int Fsconnected(struct conv *c, char *msg)
{
	if (msg != NULL && *msg != '\0')
		strncpy(c->cerr, msg, sizeof(c->cerr));

	switch (c->state) {

		case Announcing:
			c->state = Announced;
			break;

		case Connecting:
			c->state = Connected;
			break;
	}

	rendez_wakeup(&c->cr);
	return 0;
}

struct Proto *Fsrcvpcol(struct Fs *f, uint8_t proto)
{
	if (f->ipmux)
		return f->ipmux;
	else
		return f->t2p[proto];
}

struct Proto *Fsrcvpcolx(struct Fs *f, uint8_t proto)
{
	return f->t2p[proto];
}

/*
 *  called with protocol locked
 */
struct conv *Fsnewcall(struct conv *c, uint8_t * raddr, uint16_t rport,
					   uint8_t * laddr, uint16_t lport, uint8_t version)
{
	struct conv *nc;
	struct conv **l;
	int i;

	qlock(&c->qlock);
	i = 0;
	for (l = &c->incall; *l; l = &(*l)->next)
		i++;
	if (i >= Maxincall) {
		qunlock(&c->qlock);
		return NULL;
	}

	/* find a free conversation */
	nc = Fsprotoclone(c->p, network);
	if (nc == NULL) {
		qunlock(&c->qlock);
		return NULL;
	}
	ipmove(nc->raddr, raddr);
	nc->rport = rport;
	ipmove(nc->laddr, laddr);
	nc->lport = lport;
	nc->next = NULL;
	*l = nc;
	nc->state = Connected;
	nc->ipversion = version;

	qunlock(&c->qlock);

	rendez_wakeup(&c->listenr);

	return nc;
}

static long ndbwrite(struct Fs *f, char *a, uint32_t off, int n)
{
	if (off > strlen(f->ndb))
		error(Eio);
	if (off + n >= sizeof(f->ndb) - 1)
		error(Eio);
	memmove(f->ndb + off, a, n);
	f->ndb[off + n] = 0;
	f->ndbvers++;
	f->ndbmtime = seconds();
	return n;
}

uint32_t scalednconv(void)
{
	//if(conf.npage*BY2PG >= 128*MB)
	return Nchans * 4;
	//  return Nchans;
}
