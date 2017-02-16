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
#include <ip.h>

struct dev ipdevtab;

static char *devname(void)
{
	return ipdevtab.name;
}

enum {
	Qtopdir = 1,				/* top level directory */
	Qtopbase,
	Qarp = Qtopbase,
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
	BYPASS_QMAX = 64 * MiB,
	IPROUTE_LEN = 2 * PGSIZE,
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
extern struct username eve;
static long ndbwrite(struct Fs *, char *unused_char_p_t, uint32_t, int);
static void closeconv(struct conv *);
static void setup_proto_qio_bypass(struct conv *cv);
static void undo_proto_qio_bypass(struct conv *cv);

static struct conv *chan2conv(struct chan *chan)
{
	/* That's a lot of pointers to get to the conv! */
	return ipfs[chan->dev]->p[PROTO(chan->qid)]->conv[CONV(chan->qid)];
}

static inline int founddevdir(struct chan *c, struct qid q, char *n,
							  int64_t length, char *user, long perm,
							  struct dir *db)
{
	devdir(c, q, n, length, user, perm, db);
	return 1;
}

static int topdirgen(struct chan *c, struct dir *dp)
{
	struct qid q;
	mkqid(&q, QID(0, 0, Qtopdir), 0, QTDIR);
	snprintf(get_cur_genbuf(), GENBUF_SZ, "#%s%lu", devname(), c->dev);
	return founddevdir(c, q, get_cur_genbuf(), 0, network, 0555, dp);
}


static int ip3gen(struct chan *c, int i, struct dir *dp)
{
	struct qid q;
	struct conv *cv;
	char *p;
	int perm;

	cv = chan2conv(c);
	if (cv->owner == NULL)
		kstrdup(&cv->owner, eve.name);
	mkqid(&q, QID(PROTO(c->qid), CONV(c->qid), i), 0, QTFILE);

	switch (i) {
		default:
			return -1;
		case Qctl:
			return founddevdir(c, q, "ctl", 0,
					           cv->owner, cv->perm, dp);
		case Qdata:
			perm = cv->perm;
			perm |= qreadable(cv->rq) ? DMREADABLE : 0;
			perm |= qwritable(cv->wq) ? DMWRITABLE : 0;
			return founddevdir(c, q, "data", qlen(cv->rq),
							   cv->owner, perm, dp);
		case Qerr:
			perm = cv->perm;
			perm |= qreadable(cv->eq) ? DMREADABLE : 0;
			return founddevdir(c, q, "err", qlen(cv->eq),
							   cv->owner, perm, dp);
		case Qlisten:
			return founddevdir(c, q, "listen", 0, cv->owner, cv->perm, dp);
		case Qlocal:
			p = "local";
			break;
		case Qremote:
			p = "remote";
			break;
		case Qsnoop:
			if (strcmp(cv->p->name, "ipifc") != 0)
				return -1;
			perm = 0400;
			perm |= qreadable(cv->sq) ? DMREADABLE : 0;
			return founddevdir(c, q, "snoop", qlen(cv->sq),
							   cv->owner, perm, dp);
		case Qstatus:
			p = "status";
			break;
	}
	return founddevdir(c, q, p, 0, cv->owner, 0444, dp);
}

static int ip2gen(struct chan *c, int i, struct dir *dp)
{
	struct qid q;
	mkqid(&q, QID(PROTO(c->qid), 0, i), 0, QTFILE);
	switch (i) {
		case Qclone:
			return founddevdir(c, q, "clone", 0, network, 0666, dp);
		case Qstats:
			return founddevdir(c, q, "stats", 0, network, 0444, dp);
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
			if (s == DEVDOTDOT)
				return topdirgen(c, dp);
			if (s < f->np) {
				if (f->p[s]->connect == NULL)
					return 0;	/* protocol with no user interface */
				mkqid(&q, QID(s, 0, Qprotodir), 0, QTDIR);
				return founddevdir(c, q, f->p[s]->name, 0, network, 0555, dp);
			}
			s -= f->np;
			return ip1gen(c, s + Qtopbase, dp);
		case Qarp:
		case Qndb:
		case Qlog:
		case Qiproute:
		case Qiprouter:
		case Qipselftab:
			return ip1gen(c, TYPE(c->qid), dp);
		case Qprotodir:
			if (s == DEVDOTDOT)
				return topdirgen(c, dp);
			else if (s < f->p[PROTO(c->qid)]->ac) {
				cv = f->p[PROTO(c->qid)]->conv[s];
				snprintf(get_cur_genbuf(), GENBUF_SZ, "%d", s);
				mkqid(&q, QID(PROTO(c->qid), s, Qconvdir), 0, QTDIR);
				return
					founddevdir(c, q, get_cur_genbuf(), 0, cv->owner, 0555, dp);
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
		f = kzmalloc(sizeof(struct Fs), MEM_WAIT);
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
		error(EFAIL, "bad specification");

	ipgetfs(dev);
	c = devattach(devname(), spec);
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

static struct chan *ipopen(struct chan *c, int omode)
{
	ERRSTACK(2);
	struct conv *cv, *nc;
	struct Proto *p;
	int perm;
	struct Fs *f;

	/* perm is a lone rwx, not the rwx------ from the conversion */
	perm = omode_to_rwx(omode) >> 6;

	f = ipfs[c->dev];

	switch (TYPE(c->qid)) {
		default:
			break;
		case Qndb:
			if (omode & (O_WRITE | O_TRUNC) && !iseve())
				error(EPERM, ERROR_FIXME);
			if ((omode & (O_WRITE | O_TRUNC)) == (O_WRITE | O_TRUNC))
				f->ndb[0] = 0;
			break;
		case Qlog:
			netlogopen(f);
			break;
		case Qiprouter:
			iprouteropen(f);
			break;
		case Qiproute:
			c->synth_buf = kpages_zalloc(IPROUTE_LEN, MEM_WAIT);
			routeread(f, c->synth_buf, 0, IPROUTE_LEN);
			break;
		case Qtopdir:
		case Qprotodir:
		case Qconvdir:
		case Qstatus:
		case Qremote:
		case Qlocal:
		case Qstats:
		case Qipselftab:
			if (omode & O_WRITE)
				error(EPERM, ERROR_FIXME);
			break;
		case Qsnoop:
			if (omode & O_WRITE)
				error(EPERM, ERROR_FIXME);
			/* might be racy.  note the lack of a proto lock, unlike Qdata */
			p = f->p[PROTO(c->qid)];
			cv = p->conv[CONV(c->qid)];
			if (strcmp(ATTACHER(c), cv->owner) != 0 && !iseve())
				error(EPERM, ERROR_FIXME);
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
				error(ENODEV, "Null conversation from Fsprotoclone");
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
					error(EPERM, ERROR_FIXME);
				if ((perm & cv->perm) != perm)
					error(EPERM, ERROR_FIXME);

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
			/* No permissions or Announce checks required.  We'll see if that's
			 * a good idea or not. (the perm check would do nothing, as is,
			 * since an O_PATH perm is 0).
			 *
			 * But we probably want to incref to keep the conversation around
			 * until this FD/chan is closed.  #ip is a little weird in that
			 * objects never really go away (high water mark for convs, you can
			 * always find them in the ns).  I think it is possible to
			 * namec/ipgen a chan, then have that conv close, then have that
			 * chan be opened.  You can probably do this with a data file. */
			if (omode & O_PATH) {
				qlock(&cv->qlock);
				cv->inuse++;
				qunlock(&cv->qlock);
				break;
			}
			if ((perm & (cv->perm >> 6)) != perm) {
				if (strcmp(ATTACHER(c), cv->owner) != 0)
					error(EPERM, ERROR_FIXME);
				if ((perm & cv->perm) != perm)
					error(EPERM, ERROR_FIXME);

			}

			if (cv->state != Announced)
				error(EFAIL, "not announced");

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
					error(EFAIL, "listen hungup");

				qlock(&cv->listenq);
				if (waserror()) {
					qunlock(&cv->listenq);
					nexterror();
				}
				/* we can peek at incall without grabbing the cv qlock.  if
				 * anything is there, it'll remain there until we dequeue it.
				 * no one else can, since we hold the listenq lock */
				if ((c->flag & O_NONBLOCK) && !cv->incall)
					error(EAGAIN, "listen queue empty");
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
			error(EPERM, ERROR_FIXME);
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
		error(ENODATA, ERROR_FIXME);
	p = f->p[PROTO(c->qid)];
	cv = p->conv[CONV(c->qid)];
	if (!iseve() && strcmp(ATTACHER(c), cv->owner) != 0)
		error(EPERM, ERROR_FIXME);
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
			snprintf(ret, ret_l, "Qdata, %s, proto %s, conv idx %d, rq len %d, wq len %d",
			         SLIST_EMPTY(&conv->data_taps) ? "untapped" : "tapped",
			         proto->name, conv->x, qlen(conv->rq), qlen(conv->wq));
			break;
		case Qarp:
			ret = "Qarp";
			break;
		case Qiproute:
			ret = "Qiproute";
			break;
		case Qlisten:
			proto = f->p[PROTO(ch->qid)];
			conv = proto->conv[CONV(ch->qid)];
			snprintf(ret, ret_l, "Qlisten, %s proto %s, conv idx %d",
			         SLIST_EMPTY(&conv->listen_taps) ? "untapped" : "tapped",
			         proto->name, conv->x);
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
	ERRSTACK(1);
	struct conv *nc;
	struct Ipmulti *mp;

	qlock(&cv->qlock);

	if (--cv->inuse > 0) {
		qunlock(&cv->qlock);
		return;
	}
	if (waserror()) {
		qunlock(&cv->qlock);
		nexterror();
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
	if (cv->state == Bypass)
		undo_proto_qio_bypass(cv);
	cv->p->close(cv);
	cv->state = Idle;
	qunlock(&cv->qlock);
	poperror();
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
		case Qlisten:
			if (c->flag & COPEN)
				closeconv(f->p[PROTO(c->qid)]->conv[CONV(c->qid)]);
			break;
		case Qsnoop:
			if (c->flag & COPEN)
				atomic_dec(&f->p[PROTO(c->qid)]->conv[CONV(c->qid)]->snoopers);
			break;
		case Qiproute:
			if (c->flag & COPEN)
				kpages_free(c->synth_buf, IPROUTE_LEN);
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
			error(EPERM, ERROR_FIXME);
		case Qtopdir:
		case Qprotodir:
		case Qconvdir:
			return devdirread(ch, a, n, 0, 0, ipgen);
		case Qarp:
			return arpread(f->arp, a, offset, n);
		case Qndb:
			return readstr(offset, a, n, f->ndb);
		case Qiproute:
			return readmem(offset, a, n, ch->synth_buf, IPROUTE_LEN);
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
			/* this all is a bit screwed up since the size of some state's
			 * buffers will change from one invocation to another.  a reader
			 * will come in and read the entire buffer.  then it will come again
			 * and read from the next offset, expecting EOF.  if the buffer
			 * changed sizes, it'll reprint the end of the buffer slightly. */
			buf = kzmalloc(Statelen, 0);
			x = f->p[PROTO(ch->qid)];
			c = x->conv[CONV(ch->qid)];
			if (c->state == Bypass)
				snprintf(buf, Statelen, "Bypassed\n");
			else
				(*x->state)(c, buf, Statelen - 2);
			rv = readstr(offset, p, n, buf);
			kfree(buf);
			return rv;
		case Qdata:
			c = f->p[PROTO(ch->qid)]->conv[CONV(ch->qid)];
			if (ch->flag & O_NONBLOCK)
				return qread_nonblock(c->rq, a, n);
			else
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
				error(EFAIL, "stats not implemented");
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

	switch (TYPE(ch->qid)) {
		case Qdata:
			c = chan2conv(ch);
			if (ch->flag & O_NONBLOCK)
				return qbread_nonblock(c->rq, n);
			else
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
static void setluniqueport(struct conv *c, int lport)
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
		if ((xp->state == Connected || xp->state == Announced
		                            || xp->state == Bypass)
			&& xp->lport == lport
			&& xp->rport == c->rport
			&& ipcmp(xp->raddr, c->raddr) == 0
			&& ipcmp(xp->laddr, c->laddr) == 0) {
			qunlock(&p->qlock);
			error(EFAIL, "address in use");
		}
	}
	c->lport = lport;
	qunlock(&p->qlock);
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
				urandom_read(pp, sizeof(*pp));

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
static void setladdrport(struct conv *c, char *str, int announcing)
{
	char *p;
	uint16_t lport;
	uint8_t addr[IPaddrlen];

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
				error(EFAIL, "not a local IP address");
		}
	}

	/* one process can get all connections */
	if (announcing && strcmp(p, "*") == 0) {
		if (!iseve())
			error(EPERM, ERROR_FIXME);
		setluniqueport(c, 0);
	}

	lport = atoi(p);
	if (lport <= 0)
		setlport(c);
	else
		setluniqueport(c, lport);
}

static void setraddrport(struct conv *c, char *str)
{
	char *p;

	p = strchr(str, '!');
	if (p == NULL)
		error(EFAIL, "malformed address");
	*p++ = 0;
	parseip(c->raddr, str);
	c->rport = atoi(p);
	p = strchr(p, '!');
	if (p) {
		if (strstr(p, "!r") != NULL)
			c->restricted = 1;
	}
}

/*
 *  called by protocol connect routine to set addresses
 */
void Fsstdconnect(struct conv *c, char *argv[], int argc)
{
	switch (argc) {
		default:
			error(EINVAL, "bad args to %s", __func__);
		case 2:
			setraddrport(c, argv[1]);
			setladdr(c);
			setlport(c);
			break;
		case 3:
			setraddrport(c, argv[1]);
			setladdrport(c, argv[2], 0);
			break;
	}

	if ((memcmp(c->raddr, v4prefix, IPv4off) == 0 &&
		 memcmp(c->laddr, v4prefix, IPv4off) == 0)
		|| ipcmp(c->raddr, IPnoaddr) == 0)
		c->ipversion = V4;
	else
		c->ipversion = V6;
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
		error(EBUSY, ERROR_FIXME);
	c->state = Connecting;
	c->cerr[0] = '\0';
	if (x->connect == NULL)
		error(EFAIL, "connect not supported");
	x->connect(c, cb->f, cb->nf);

	qunlock(&c->qlock);
	if (waserror()) {
		qlock(&c->qlock);
		nexterror();
	}
	rendez_sleep(&c->cr, connected, c);
	qlock(&c->qlock);
	poperror();

	if (c->cerr[0] != '\0')
		error(EFAIL, c->cerr);
}

/*
 *  called by protocol announce routine to set addresses
 */
void Fsstdannounce(struct conv *c, char *argv[], int argc)
{
	memset(c->raddr, 0, sizeof(c->raddr));
	c->rport = 0;
	switch (argc) {
		default:
			error(EINVAL, "bad args to announce");
		case 2:
			setladdrport(c, argv[1], 1);
			break;
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
		error(EBUSY, ERROR_FIXME);
	c->state = Announcing;
	c->cerr[0] = '\0';
	if (x->announce == NULL)
		error(EFAIL, "announce not supported");
	x->announce(c, cb->f, cb->nf);

	qunlock(&c->qlock);
	if (waserror()) {
		qlock(&c->qlock);
		nexterror();
	}
	rendez_sleep(&c->cr, announced, c);
	qlock(&c->qlock);
	poperror();

	if (c->cerr[0] != '\0')
		error(EFAIL, c->cerr);
}

/*
 *  called by protocol bind routine to set addresses
 */
void Fsstdbind(struct conv *c, char *argv[], int argc)
{
	switch (argc) {
		default:
			error(EINVAL, "bad args to bind");
		case 2:
			setladdrport(c, argv[1], 0);
			break;
	}
}

static void bindctlmsg(struct Proto *x, struct conv *c, struct cmdbuf *cb)
{
	if (x->bind == NULL)
		Fsstdbind(c, cb->f, cb->nf);
	else
		x->bind(c, cb->f, cb->nf);
}

/* Helper, called by protocols to use the bypass.
 *
 * This is a bit nasty due to the overall nastiness of #ip.  We need to lock
 * before checking the state and hold the qlock throughout, because a concurrent
 * closeconv() could tear down the bypass.  Specifically, it could free the
 * bypass queues.  The root issue is that conversation lifetimes are not managed
 * well.
 *
 * If we fail, it's our responsibility to consume (free) the block(s). */
void bypass_or_drop(struct conv *cv, struct block *bp)
{
	qlock(&cv->qlock);
	if (cv->state == Bypass)
		qpass(cv->rq, bp);
	else
		freeblist(bp);
	qunlock(&cv->qlock);
}

/* Push the block directly to the approprite ipoput function.
 *
 * It's the protocol's responsibility (and thus ours here) to make sure there is
 * at least the right amount of the IP header in the block (ipoput{4,6} assumes
 * it has the right amount, and the other protocols account for the IP header in
 * their own header).
 *
 * For the TTL and TOS, we just use the default ones.  If we want, we could look
 * into the actual block and see what the user wanted, though we're bypassing
 * the protocol layer, not the IP layer. */
static void proto_bypass_kick(void *arg, struct block *bp)
{
	struct conv *cv = (struct conv*)arg;
	uint8_t vers_nibble;
	struct Fs *f;

	f = cv->p->f;

	bp = pullupblock(bp, 1);
	if (!bp)
		error(EINVAL, "Proto bypass unable to pullup a byte!");
	vers_nibble = *(uint8_t*)bp->rp & 0xf0;
	switch (vers_nibble) {
	case IP_VER4:
		bp = pullupblock(bp, IPV4HDR_LEN);
		if (!bp)
			error(EINVAL, "Proto bypass unable to pullup v4 header");
		ipoput4(f, bp, FALSE, MAXTTL, DFLTTOS, NULL);
		break;
	case IP_VER6:
		bp = pullupblock(bp, IPV6HDR_LEN);
		if (!bp)
			error(EINVAL, "Proto bypass unable to pullup v6 header");
		ipoput6(f, bp, FALSE, MAXTTL, DFLTTOS, NULL);
		break;
	default:
		error(EINVAL, "Proto bypass block had unknown IP version 0x%x",
		      vers_nibble);
	}
}

/* Sets up cv for the protocol bypass.  We use different queues for two reasons:
 * 1) To be protocol independent.  For instance, TCP and UDP could use very
 * different QIO styles.
 * 2) To set up our own kick/bypass method.  Note how udpcreate() and here uses
 * qbypass() (just blast it out), while TCP uses qopen() with a kick.  TCP still
 * follows queuing discipline.
 *
 * It's like we are our own protocol, the bypass protocol, when it comes to how
 * we interact with qio.  The conv still is of the real protocol type (e.g.
 * TCP).
 *
 * Note that we can't free the old queues.  The way #ip works, the queues are
 * created when the conv is created, but the conv is never freed.  It's like a
 * slab allocator that never frees objects, but just reinitializes them a
 * little.
 *
 * For the queues, we're basically like UDP:
 * - We take packets for rq and drop on overflow.
 * - rq is also Qmsg, but we also have Qcoalesce, to ignore out zero-len blocks
 * - We kick for our outbound (wq) messages.
 *
 * Note that Qmsg can drop parts of packets.  It's up to the user to read
 * enough.  If they didn't read enough, the extra is dropped.  This is similar
 * to SOCK_DGRAM and recvfrom().  Minus major changes, there's no nice way to
 * get individual messages with read().  Userspace using the bypass will need to
 * find out the MTU of the NIC the IP stack is attached to, and make sure to
 * read in at least that amount each time. */
static void setup_proto_qio_bypass(struct conv *cv)
{
	cv->rq_save = cv->rq;
	cv->wq_save = cv->wq;
	cv->rq = qopen(BYPASS_QMAX, Qmsg | Qcoalesce, 0, 0);
	cv->wq = qbypass(proto_bypass_kick, cv);
}

static void undo_proto_qio_bypass(struct conv *cv)
{
	qfree(cv->rq);
	qfree(cv->wq);
	cv->rq = cv->rq_save;
	cv->wq = cv->wq_save;
	cv->rq_save = NULL;
	cv->wq_save = NULL;
}

void Fsstdbypass(struct conv *cv, char *argv[], int argc)
{
	memset(cv->raddr, 0, sizeof(cv->raddr));
	cv->rport = 0;
	switch (argc) {
	case 2:
		setladdrport(cv, argv[1], 1);
		break;
	default:
		error(EINVAL, "Bad args (was %d, need 2) to bypass", argc);
	}
}

static void bypassctlmsg(struct Proto *x, struct conv *cv, struct cmdbuf *cb)
{
	if (!x->bypass)
		error(EFAIL, "Protocol %s does not support bypass", x->name);
	/* The protocol needs to set the port (usually by calling Fsstdbypass) and
	 * then do whatever it needs to make sure it can find the conv again during
	 * receive (usually by adding to a hash table). */
	x->bypass(cv, cb->f, cb->nf);
	setup_proto_qio_bypass(cv);
	cv->state = Bypass;
}

static void shutdownctlmsg(struct conv *cv, struct cmdbuf *cb)
{
	if (cb->nf < 2)
		goto err;
	if (!strcmp(cb->f[1], "rd")) {
		qhangup(cv->rq, "shutdown");
		if (cv->p->shutdown)
			cv->p->shutdown(cv, SHUT_RD);
	} else if (!strcmp(cb->f[1], "wr")) {
		qhangup(cv->wq, "shutdown");
		if (cv->p->shutdown)
			cv->p->shutdown(cv, SHUT_WR);
	} else if (!strcmp(cb->f[1], "rdwr")) {
		qhangup(cv->rq, "shutdown");
		qhangup(cv->wq, "shutdown");
		if (cv->p->shutdown)
			cv->p->shutdown(cv, SHUT_RDWR);
	} else {
		goto err;
	}
	return;
err:
	error(EINVAL, "shutdown [rx|tx|rxtx]");
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

/* Binds a conversation, as if the user wrote "bind *" into ctl. */
static void autobind(struct conv *cv)
{
	ERRSTACK(1);
	struct cmdbuf *cb;

	cb = parsecmd("bind *", 7);
	if (waserror()) {
		kfree(cb);
		nexterror();
	}
	bindctlmsg(cv->p, cv, cb);
	poperror();
	kfree(cb);
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
			error(EPERM, ERROR_FIXME);
		case Qdata:
			x = f->p[PROTO(ch->qid)];
			c = x->conv[CONV(ch->qid)];
			/* connection-less protocols (UDP) can write without manually
			 * binding. */
			if (c->lport == 0)
				autobind(c);
			if (ch->flag & O_NONBLOCK)
				qwrite_nonblock(c->wq, a, n);
			else
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
				error(EFAIL, "short control request");
			if (strcmp(cb->f[0], "connect") == 0)
				connectctlmsg(x, c, cb);
			else if (strcmp(cb->f[0], "announce") == 0)
				announcectlmsg(x, c, cb);
			else if (strcmp(cb->f[0], "bind") == 0)
				bindctlmsg(x, c, cb);
			else if (strcmp(cb->f[0], "bypass") == 0)
				bypassctlmsg(x, c, cb);
			else if (strcmp(cb->f[0], "shutdown") == 0)
				shutdownctlmsg(c, cb);
			else if (strcmp(cb->f[0], "ttl") == 0)
				ttlctlmsg(c, cb);
			else if (strcmp(cb->f[0], "tos") == 0)
				tosctlmsg(c, cb);
			else if (strcmp(cb->f[0], "ignoreadvice") == 0)
				c->ignoreadvice = 1;
			else if (strcmp(cb->f[0], "addmulti") == 0) {
				if (cb->nf < 2)
					error(EFAIL, "addmulti needs interface address");
				if (cb->nf == 2) {
					if (!ipismulticast(c->raddr))
						error(EFAIL, "addmulti for a non multicast address");
					parseip(ia, cb->f[1]);
					ipifcaddmulti(c, c->raddr, ia);
				} else {
					parseip(ma, cb->f[2]);
					if (!ipismulticast(ma))
						error(EFAIL, "addmulti for a non multicast address");
					parseip(ia, cb->f[1]);
					ipifcaddmulti(c, ma, ia);
				}
			} else if (strcmp(cb->f[0], "remmulti") == 0) {
				if (cb->nf < 2)
					error(EFAIL, "remmulti needs interface address");
				if (!ipismulticast(c->raddr))
					error(EFAIL, "remmulti for a non multicast address");
				parseip(ia, cb->f[1]);
				ipifcremmulti(c, c->raddr, ia);
			} else if (x->ctl != NULL) {
				x->ctl(c, cb->f, cb->nf);
			} else
				error(EFAIL, "unknown control request");
			qunlock(&c->qlock);
			kfree(cb);
			poperror();
	}
	return n;
}

static long ipbwrite(struct chan *ch, struct block *bp, uint32_t offset)
{
	struct conv *c;
	int n;

	switch (TYPE(ch->qid)) {
		case Qdata:
			c = chan2conv(ch);
			if (bp->next)
				bp = concatblock(bp);
			n = BLEN(bp);
			if (ch->flag & O_NONBLOCK)
				qbwrite_nonblock(c->wq, bp);
			else
				qbwrite(c->wq, bp);
			return n;
		default:
			return devbwrite(ch, bp, offset);
	}
}

static void ip_wake_cb(struct queue *q, void *data, int filter)
{
	struct conv *conv = (struct conv*)data;
	struct fd_tap *tap_i;
	/* For these two, we want to ignore events on the opposite end of the
	 * queues.  For instance, we want to know when the WQ is writable.  Our
	 * writes will actually make it readable - we don't want to trigger a tap
	 * for that.  However, qio doesn't know how/why we are using a queue, or
	 * even who the ends are (hence the callbacks) */
	if ((filter & FDTAP_FILT_READABLE) && (q == conv->wq))
		return;
	if ((filter & FDTAP_FILT_WRITABLE) && (q == conv->rq))
		return;
	/* At this point, we have an event we want to send to our taps (if any).
	 * The lock protects list integrity and the existence of the tap.
	 *
	 * Previously, I thought of using the conv qlock.  That actually breaks, due
	 * to weird usages of the qlock (someone holds it for a long time, blocking
	 * the inbound wakeup from etherread4).
	 *
	 * I opted for a spinlock for a couple reasons:
	 * - fire_tap should not block.  ideally it'll be fast too (it's mostly a
	 * send_event).
	 * - our callers might not want to block.  A lot of network wakeups will
	 * come network processes (etherread4) or otherwise unrelated to this
	 * particular conversation.  I'd rather do something like fire off a KMSG
	 * than block those.
	 * - if fire_tap takes a while, holding the lock only slows down other
	 * events on this *same* conversation, or other tap registration.  not a
	 * huge deal. */
	spin_lock(&conv->tap_lock);
	SLIST_FOREACH(tap_i, &conv->data_taps, link)
		fire_tap(tap_i, filter);
	spin_unlock(&conv->tap_lock);
}

int iptapfd(struct chan *chan, struct fd_tap *tap, int cmd)
{
	struct conv *conv = chan2conv(chan);
	int ret;

	#define DEVIP_LEGAL_DATA_TAPS (FDTAP_FILT_READABLE | FDTAP_FILT_WRITABLE | \
	                               FDTAP_FILT_HANGUP | FDTAP_FILT_PRIORITY |   \
	                               FDTAP_FILT_ERROR)
	#define DEVIP_LEGAL_LISTEN_TAPS (FDTAP_FILT_READABLE | FDTAP_FILT_HANGUP)

	switch (TYPE(chan->qid)) {
		case Qdata:
			if (tap->filter & ~DEVIP_LEGAL_DATA_TAPS) {
				set_errno(ENOSYS);
				set_errstr("Unsupported #%s data tap %p, must be %p", devname(),
				           tap->filter, DEVIP_LEGAL_DATA_TAPS);
				return -1;
			}
			spin_lock(&conv->tap_lock);
			switch (cmd) {
				case (FDTAP_CMD_ADD):
					if (SLIST_EMPTY(&conv->data_taps)) {
						qio_set_wake_cb(conv->rq, ip_wake_cb, conv);
						qio_set_wake_cb(conv->wq, ip_wake_cb, conv);
					}
					SLIST_INSERT_HEAD(&conv->data_taps, tap, link);
					ret = 0;
					break;
				case (FDTAP_CMD_REM):
					SLIST_REMOVE(&conv->data_taps, tap, fd_tap, link);
					if (SLIST_EMPTY(&conv->data_taps)) {
						qio_set_wake_cb(conv->rq, 0, conv);
						qio_set_wake_cb(conv->wq, 0, conv);
					}
					ret = 0;
					break;
				default:
					set_errno(ENOSYS);
					set_errstr("Unsupported #%s data tap command %p",
					           devname(), cmd);
					ret = -1;
			}
			spin_unlock(&conv->tap_lock);
			return ret;
		case Qlisten:
			if (tap->filter & ~DEVIP_LEGAL_LISTEN_TAPS) {
				set_errno(ENOSYS);
				set_errstr("Unsupported #%s listen tap %p, must be %p",
				           devname(), tap->filter, DEVIP_LEGAL_LISTEN_TAPS);
				return -1;
			}
			spin_lock(&conv->tap_lock);
			switch (cmd) {
				case (FDTAP_CMD_ADD):
					SLIST_INSERT_HEAD(&conv->listen_taps, tap, link);
					ret = 0;
					break;
				case (FDTAP_CMD_REM):
					SLIST_REMOVE(&conv->listen_taps, tap, fd_tap, link);
					ret = 0;
					break;
				default:
					set_errno(ENOSYS);
					set_errstr("Unsupported #%s listen tap command %p",
					           devname(), cmd);
					ret = -1;
			}
			spin_unlock(&conv->tap_lock);
			return ret;
		default:
			set_errno(ENOSYS);
			set_errstr("Can't tap #%s file type %d", devname(),
			           TYPE(chan->qid));
			return -1;
	}
}

struct dev ipdevtab __devtab = {
	.name = "ip",

	.reset = ipreset,
	.init = ipinit,
	.shutdown = devshutdown,
	.attach = ipattach,
	.walk = ipwalk,
	.stat = ipstat,
	.open = ipopen,
	.create = devcreate,
	.close = ipclose,
	.read = ipread,
	.bread = ipbread,
	.write = ipwrite,
	.bwrite = ipbwrite,
	.remove = devremove,
	.wstat = ipwstat,
	.power = devpower,
	.chaninfo = ipchaninfo,
	.tapfd = iptapfd,
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
				error(ENOMEM,
				      "conv kzmalloc(%d, 0) failed in Fsprotoclone",
				      sizeof(struct conv));
			qlock_init(&c->qlock);
			qlock_init(&c->listenq);
			rendez_init(&c->cr);
			rendez_init(&c->listenr);
			SLIST_INIT(&c->data_taps);	/* already = 0; set to be futureproof */
			SLIST_INIT(&c->listen_taps);
			spinlock_init(&c->tap_lock);
			qlock(&c->qlock);
			c->p = p;
			c->x = pp - p->conv;
			if (p->ptclsize != 0) {
				c->ptcl = kzmalloc(p->ptclsize, 0);
				if (c->ptcl == NULL) {
					kfree(c);
					error(ENOMEM,
					      "ptcl kzmalloc(%d, 0) failed in Fsprotoclone",
					      p->ptclsize);
				}
			}
			*pp = c;
			p->ac++;
			c->eq = qopen(1024, Qmsg, 0, 0);
			(*p->create) (c);
			assert(c->rq && c->wq);
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
		strlcpy(c->cerr, msg, sizeof(c->cerr));

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

static void fire_listener_taps(struct conv *conv)
{
	struct fd_tap *tap_i;
	if (SLIST_EMPTY(&conv->listen_taps))
		return;
	spin_lock(&conv->tap_lock);
	SLIST_FOREACH(tap_i, &conv->listen_taps, link)
		fire_tap(tap_i, FDTAP_FILT_READABLE);
	spin_unlock(&conv->tap_lock);
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
	fire_listener_taps(c);

	return nc;
}

static long ndbwrite(struct Fs *f, char *a, uint32_t off, int n)
{
	if (off > strlen(f->ndb))
		error(EIO, ERROR_FIXME);
	if (off + n >= sizeof(f->ndb) - 1)
		error(EIO, ERROR_FIXME);
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
