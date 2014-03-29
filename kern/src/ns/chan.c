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
#include <syscall.h>

char *channame(struct chan *c)
{	/* DEBUGGING */
	if (c == NULL)
		return "<NULL chan>";
	if (c->name == NULL)
		return "<NULL name>";
	if (c->name->s == NULL)
		return "<NULL name.s>";
	return c->name->s;
}

enum {
	CNAMESLOP = 20
};

struct {
	spinlock_t lock;
	int fid;
	struct chan *free;
	struct chan *list;
} chanalloc;

typedef struct Elemlist Elemlist;

struct Elemlist {
	char *name;					/* copy of name, so '/' can be overwritten */
	int ARRAY_SIZEs;
	char **elems;
	int *off;
	int mustbedir;
};

#define SEP(c) ((c) == 0 || (c) == '/')
void cleancname(struct cname *);

int isdotdot(char *p)
{
	return p[0] == '.' && p[1] == '.' && p[2] == '\0';
}

int emptystr(char *s)
{
	if (s == NULL)
		return 1;
	if (s[0] == '\0')
		return 1;
	return 0;
}

/*
 * Atomically replace *p with copy of s
 */
void kstrdup(char **p, char *s)
{
	int n;
	char *t, *prev;

	n = strlen(s) + 1;
	/* if it's a user, we can wait for memory; if not, something's very wrong */
	if (current) {
		t = kzmalloc(n, 0);
	} else {
		t = kzmalloc(n, 0);
		if (t == NULL)
			panic("kstrdup: no memory");
	}
	memmove(t, s, n);
	prev = *p;
	*p = t;
	kfree(prev);
}

void chandevreset(void)
{
	int i;

	for (i = 0; &devtab[i] < __devtabend; i++)
		devtab[i].reset();
}

void chandevinit(void)
{
	int i;

	for (i = 0; &devtab[i] < __devtabend; i++)
		devtab[i].init();
}

void chandevshutdown(void)
{
	int i;

	/* shutdown in reverse order */
	for (i = 0; &devtab[i] < __devtabend; i++) ;
	for (i--; i >= 0; i--)
		devtab[i].shutdown();
}

static void chan_release(struct kref *kref)
{
	struct chan *c = container_of(kref, struct chan, ref);
	ERRSTACK(1);
	/* this style discards the error from close().  picture it as
	 * if (waserror()) { } else { close(); } chanfree_no_matter_what();  */
	if (!waserror()) {
		printd("releasing chan %p, type %d\n", c, c->type);
		/* -1 means there is no dev yet.  wants a noop for close() */
		if (c->type != -1)
			devtab[c->type].close(c);
	}
	/* need to poperror regardless of whether we error'd or not */
	poperror();
	/* and chan free no matter what */
	chanfree(c);
}

struct chan *newchan(void)
{
	struct chan *c;

	spin_lock(&(&chanalloc)->lock);
	c = chanalloc.free;
	if (c != 0)
		chanalloc.free = c->next;
	spin_unlock(&(&chanalloc)->lock);

	if (c == NULL) {
		c = kzmalloc(sizeof(struct chan), 0);
		spin_lock(&(&chanalloc)->lock);
		c->fid = ++chanalloc.fid;
		c->link = chanalloc.list;
		chanalloc.list = c;
		spin_unlock(&(&chanalloc)->lock);
		spinlock_init(&c->lock);
		qlock_init(&c->umqlock);
	}

	/* if you get an error before associating with a dev, cclose skips calling
	 * the dev's close */
	c->type = -1;
	c->flag = 0;
	kref_init(&c->ref, chan_release, 1);
	c->dev = 0;
	c->offset = 0;
	c->iounit = 0;
	c->umh = 0;
	c->uri = 0;
	c->dri = 0;
	c->aux = 0;
	c->mchan = 0;
	c->mcp = 0;
	c->mux = 0;
	c->mqid.path = 0;
	c->mqid.vers = 0;
	c->mqid.type = 0;
	c->name = 0;
	return c;
}

static void __cname_release(struct kref *kref)
{
	struct cname *n = container_of(kref, struct cname, ref);
	kfree(n->s);
	kfree(n);
}

struct cname *newcname(char *s)
{
	struct cname *n;
	int i;

	n = kzmalloc(sizeof(*n), 0);
	i = strlen(s);
	n->len = i;
	n->alen = i + CNAMESLOP;
	n->s = kzmalloc(n->alen, 0);
	memmove(n->s, s, i + 1);
	kref_init(&n->ref, __cname_release, 1);
	return n;
}

void cnameclose(struct cname *n)
{
	if (n == NULL)
		return;
	kref_put(&n->ref);
}

struct cname *addelem(struct cname *n, char *s)
{
	int i, a;
	char *t;
	struct cname *new;

	if (s[0] == '.' && s[1] == '\0')
		return n;

	if (kref_refcnt(&n->ref) > 1) {
		/* copy on write */
		new = newcname(n->s);
		cnameclose(n);
		n = new;
	}

	i = strlen(s);
	if (n->len + 1 + i + 1 > n->alen) {
		a = n->len + 1 + i + 1 + CNAMESLOP;
		t = kzmalloc(a, 0);
		memmove(t, n->s, n->len + 1);
		kfree(n->s);
		n->s = t;
		n->alen = a;
	}
	if (n->len > 0 && n->s[n->len - 1] != '/' && s[0] != '/')	/* don't insert extra slash if one is present */
		n->s[n->len++] = '/';
	memmove(n->s + n->len, s, i + 1);
	n->len += i;
	if (isdotdot(s))
		cleancname(n);
	return n;
}

void chanfree(struct chan *c)
{
	c->flag = CFREE;

	if (c->umh != NULL) {
		putmhead(c->umh);
		c->umh = NULL;
	}
	if (c->umc != NULL) {
		cclose(c->umc);
		c->umc = NULL;
	}
	if (c->mux != NULL) {
		//
		muxclose(c->mux);
		c->mux = NULL;
	}
	if (c->mchan != NULL) {
		cclose(c->mchan);
		c->mchan = NULL;
	}

	cnameclose(c->name);

	spin_lock(&(&chanalloc)->lock);
	c->next = chanalloc.free;
	chanalloc.free = c;
	spin_unlock(&(&chanalloc)->lock);
}

void cclose(struct chan *c)
{
	if (c == 0)
		return;

	if (c->flag & CFREE)
		panic("cclose %p", getcallerpc(&c));

	kref_put(&c->ref);
}

/*
 * Make sure we have the only copy of c.  (Copy on write.)
 */
struct chan *cunique(struct chan *c)
{
	struct chan *nc;

	if (kref_refcnt(&c->ref) != 1) {
		nc = cclone(c);
		cclose(c);
		c = nc;
	}

	return c;
}

int eqqid(struct qid a, struct qid b)
{
	return a.path == b.path && a.vers == b.vers;
}

int eqchan(struct chan *a, struct chan *b, int pathonly)
{
	if (a->qid.path != b->qid.path)
		return 0;
	if (!pathonly && a->qid.vers != b->qid.vers)
		return 0;
	if (a->type != b->type)
		return 0;
	if (a->dev != b->dev)
		return 0;
	return 1;
}

int eqchantdqid(struct chan *a, int type, int dev, struct qid qid, int pathonly)
{
	if (a->qid.path != qid.path)
		return 0;
	if (!pathonly && a->qid.vers != qid.vers)
		return 0;
	if (a->type != type)
		return 0;
	if (a->dev != dev)
		return 0;
	return 1;
}

static void mh_release(struct kref *kref)
{
	struct mhead *mh = container_of(kref, struct mhead, ref);
	mh->mount = (struct mount *)0xCafeBeef;
	kfree(mh);
}

struct mhead *newmhead(struct chan *from)
{
	struct mhead *mh;

	mh = kzmalloc(sizeof(struct mhead), 0);
	kref_init(&mh->ref, mh_release, 1);
	mh->from = from;
	kref_get(&from->ref, 1);

/*
	n = from->name->len;
	if(n >= sizeof(mh->fromname))
		n = sizeof(mh->fromname)-1;
	memmove(mh->fromname, from->name->s, n);
	mh->fromname[n] = 0;
*/
	return mh;
}

int cmount(struct chan *new, struct chan *old, int flag, char *spec)
{
	ERRSTACK(1);
	struct pgrp *pg;
	int order, flg;
	struct mhead *m, **l, *mh;
	struct mount *nm, *f, *um, **h;

	if (QTDIR & (old->qid.type ^ new->qid.type))
		error(Emount);

	if (old->umh)
		printd("cmount old extra umh\n");

	order = flag & MORDER;

	if ((old->qid.type & QTDIR) == 0 && order != MREPL)
		error(Emount);

	mh = new->umh;

	/*
	 * Not allowed to bind when the old directory
	 * is itself a union.  (Maybe it should be allowed, but I don't see
	 * what the semantics would be.)
	 *
	 * We need to check mh->mount->next to tell unions apart from
	 * simple mount points, so that things like
	 *  mount -c fd /root
	 *  bind -c /root /
	 * work.  The check of mount->mflag catches things like
	 *  mount fd /root
	 *  bind -c /root /
	 * 
	 * This is far more complicated than it should be, but I don't
	 * see an easier way at the moment.     -rsc
	 */
	if ((flag & MCREATE) && mh && mh->mount
		&& (mh->mount->next || !(mh->mount->mflag & MCREATE)))
		error(Emount);

	pg = current->pgrp;
	wlock(&pg->ns);

	l = &MOUNTH(pg, old->qid);
	for (m = *l; m; m = m->hash) {
		if (eqchan(m->from, old, 1))
			break;
		l = &m->hash;
	}

	if (m == NULL) {
		/*
		 *  nothing mounted here yet.  create a mount
		 *  head and add to the hash table.
		 */
		m = newmhead(old);
		*l = m;

		/*
		 *  if this is a union mount, add the old
		 *  node to the mount chain.
		 */
		if (order != MREPL)
			m->mount = newmount(m, old, 0, 0);
	}
	wlock(&m->lock);
	if (waserror()) {
		wunlock(&m->lock);
		nexterror();
	}
	wunlock(&pg->ns);

	nm = newmount(m, new, flag, spec);
	if (mh != NULL && mh->mount != NULL) {
		/*
		 *  copy a union when binding it onto a directory
		 */
		flg = order;
		if (order == MREPL)
			flg = MAFTER;
		h = &nm->next;
		um = mh->mount;
		for (um = um->next; um; um = um->next) {
			f = newmount(m, um->to, flg, um->spec);
			*h = f;
			h = &f->next;
		}
	}

	if (m->mount && order == MREPL) {
		mountfree(m->mount);
		m->mount = 0;
	}

	if (flag & MCREATE)
		nm->mflag |= MCREATE;

	if (m->mount && order == MAFTER) {
		for (f = m->mount; f->next; f = f->next) ;
		f->next = nm;
	} else {
		for (f = nm; f->next; f = f->next) ;
		f->next = m->mount;
		m->mount = nm;
	}

	wunlock(&m->lock);
	poperror();
	return nm->mountid;
}

void cunmount(struct chan *mnt, struct chan *mounted)
{
	struct pgrp *pg;
	struct mhead *m, **l;
	struct mount *f, **p;

	if (mnt->umh)	/* should not happen */
		printd("cunmount newp extra umh %p has %p\n", mnt, mnt->umh);

	/*
	 * It _can_ happen that mounted->umh is non-NULL, 
	 * because mounted is the result of namec(Aopen)
	 * (see sysfile.c:/^sysunmount).
	 * If we open a union directory, it will have a umh.
	 * Although surprising, this is okay, since the
	 * cclose will take care of freeing the umh.
	 */

	pg = current->pgrp;
	wlock(&pg->ns);

	l = &MOUNTH(pg, mnt->qid);
	for (m = *l; m; m = m->hash) {
		if (eqchan(m->from, mnt, 1))
			break;
		l = &m->hash;
	}

	if (m == 0) {
		wunlock(&pg->ns);
		error(Eunmount);
	}

	wlock(&m->lock);
	if (mounted == 0) {
		*l = m->hash;
		wunlock(&pg->ns);
		mountfree(m->mount);
		m->mount = NULL;
		cclose(m->from);
		wunlock(&m->lock);
		putmhead(m);
		return;
	}

	p = &m->mount;
	for (f = *p; f; f = f->next) {
		/* BUG: Needs to be 2 pass */
		if (eqchan(f->to, mounted, 1) ||
			(f->to->mchan && eqchan(f->to->mchan, mounted, 1))) {
			*p = f->next;
			f->next = 0;
			mountfree(f);
			if (m->mount == NULL) {
				*l = m->hash;
				cclose(m->from);
				wunlock(&m->lock);
				wunlock(&pg->ns);
				putmhead(m);
				return;
			}
			wunlock(&m->lock);
			wunlock(&pg->ns);
			return;
		}
		p = &f->next;
	}
	wunlock(&m->lock);
	wunlock(&pg->ns);
	error(Eunion);
}

struct chan *cclone(struct chan *c)
{
	struct chan *nc;
	struct walkqid *wq;

	wq = devtab[c->type].walk(c, NULL, NULL, 0);
	if (wq == NULL)
		error("clone failed");
	nc = wq->clone;
	kfree(wq);
	nc->name = c->name;
	if (c->name)
		kref_get(&c->name->ref, 1);
	return nc;
}

int
findmount(struct chan **cp,
		  struct mhead **mp, int type, int dev, struct qid qid)
{
	struct pgrp *pg;
	struct mhead *m;

	pg = current->pgrp;
	rlock(&pg->ns);
	for (m = MOUNTH(pg, qid); m; m = m->hash) {
		rlock(&m->lock);
		if (m->from == NULL) {
			printd("m %p m->from 0\n", m);
			runlock(&m->lock);
			continue;
		}
		if (eqchantdqid(m->from, type, dev, qid, 1)) {
			runlock(&pg->ns);
			if (mp != NULL) {
				kref_get(&m->ref, 1);
				if (*mp != NULL)
					putmhead(*mp);
				*mp = m;
			}
			if (*cp != NULL)
				cclose(*cp);
			kref_get(&m->mount->to->ref, 1);
			*cp = m->mount->to;
			runlock(&m->lock);
			return 1;
		}
		runlock(&m->lock);
	}

	runlock(&pg->ns);
	return 0;
}

int domount(struct chan **cp, struct mhead **mp)
{
	return findmount(cp, mp, (*cp)->type, (*cp)->dev, (*cp)->qid);
}

struct chan *undomount(struct chan *c, struct cname *name)
{
	ERRSTACK(1);
	struct chan *nc;
	struct pgrp *pg;
	struct mount *t;
	struct mhead **h, **he, *f;

	pg = current->pgrp;
	rlock(&pg->ns);
	if (waserror()) {
		runlock(&pg->ns);
		nexterror();
	}

	he = &pg->mnthash[MNTHASH];
	for (h = pg->mnthash; h < he; h++) {
		for (f = *h; f; f = f->hash) {
			if (strcmp(f->from->name->s, name->s) != 0)
				continue;
			for (t = f->mount; t; t = t->next) {
				if (eqchan(c, t->to, 1)) {
					/*
					 * We want to come out on the left hand side of the mount
					 * point using the element of the union that we entered on.
					 * To do this, find the element that has a from name of
					 * c->name->s.
					 */
					if (strcmp(t->head->from->name->s, name->s) != 0)
						continue;
					nc = t->head->from;
					kref_get(&nc->ref, 1);
					cclose(c);
					c = nc;
					break;
				}
			}
		}
	}
	poperror();
	runlock(&pg->ns);
	return c;
}

/*
 * Either walks all the way or not at all.  No partial results in *cp.
 * *nerror is the number of names to display in an error message.
 */
static char Edoesnotexist[] = "does not exist";
int walk(struct chan **cp, char **names, int nnames, int nomount, int *nerror)
{
	int dev, dotdot, i, n, nhave, ntry, type;
	struct chan *c, *nc;
	struct cname *cname;
	struct mount *f;
	struct mhead *mh, *nmh;
	struct walkqid *wq;

	c = *cp;
	kref_get(&c->ref, 1);
	cname = c->name;
	kref_get(&cname->ref, 1);
	mh = NULL;

	/*
	 * While we haven't gotten all the way down the path:
	 *    1. step through a mount po int unused_int, if any
	 *    2. send a walk request for initial dotdot or initial prefix without dotdot
	 *    3. move to the first mountpoint along the way.
	 *    4. repeat.
	 *
	 * An invariant is that each time through the loop, c is on the undomount
	 * side of the mount po int unused_int, and c's name is cname.
	 */
	for (nhave = 0; nhave < nnames; nhave += n) {
		if ((c->qid.type & QTDIR) == 0) {
			if (nerror)
				*nerror = nhave;
			cnameclose(cname);
			cclose(c);
			set_errstr(Enotdir);
			if (mh != NULL)
				putmhead(mh);
			return -1;
		}
		ntry = nnames - nhave;
		if (ntry > MAXWELEM)
			ntry = MAXWELEM;
		dotdot = 0;
		for (i = 0; i < ntry; i++) {
			if (isdotdot(names[nhave + i])) {
				if (i == 0) {
					dotdot = 1;
					ntry = 1;
				} else
					ntry = i;
				break;
			}
		}

		if (!dotdot && !nomount)
			domount(&c, &mh);

		type = c->type;
		dev = c->dev;

		if ((wq = devtab[type].walk(c, NULL, names + nhave, ntry)) == NULL) {
			/* try a union mount, if any */
			if (mh && !nomount) {
				/*
				 * mh->mount == c, so start at mh->mount->next
				 */
				rlock(&mh->lock);
				for (f = mh->mount->next; f; f = f->next)
					if ((wq =
						 devtab[f->to->type].walk(f->to, NULL, names + nhave,
												  ntry)) != NULL)
						break;
				runlock(&mh->lock);
				if (f != NULL) {
					type = f->to->type;
					dev = f->to->dev;
				}
			}
			if (wq == NULL) {
				cclose(c);
				cnameclose(cname);
				if (nerror)
					*nerror = nhave + 1;
				if (mh != NULL)
					putmhead(mh);
				return -1;
			}
		}

		nmh = NULL;
		if (dotdot) {
			assert(wq->nqid == 1);
			assert(wq->clone != NULL);

			cname = addelem(cname, "..");
			nc = undomount(wq->clone, cname);
			n = 1;
		} else {
			nc = NULL;
			if (!nomount)
				for (i = 0; i < wq->nqid && i < ntry - 1; i++)
					if (findmount(&nc, &nmh, type, dev, wq->qid[i]))
						break;
			if (nc == NULL) {	/* no mount points along path */
				if (wq->clone == NULL) {
					cclose(c);
					cnameclose(cname);
					if (wq->nqid == 0 || (wq->qid[wq->nqid - 1].type & QTDIR)) {
						if (nerror)
							*nerror = nhave + wq->nqid + 1;
						set_errstr(Edoesnotexist);
					} else {
						if (nerror)
							*nerror = nhave + wq->nqid;
						set_errstr(Enotdir);
					}
					kfree(wq);
					if (mh != NULL)
						putmhead(mh);
					return -1;
				}
				n = wq->nqid;
				nc = wq->clone;
			} else {	/* stopped early, at a mount point */
				if (wq->clone != NULL) {
					cclose(wq->clone);
					wq->clone = NULL;
				}
				n = i + 1;
			}
			for (i = 0; i < n; i++)
				cname = addelem(cname, names[nhave + i]);
		}
		cclose(c);
		c = nc;
		putmhead(mh);
		mh = nmh;
		kfree(wq);
	}

	putmhead(mh);

	c = cunique(c);

	if (c->umh != NULL) {	//BUG
		printd("walk umh\n");
		putmhead(c->umh);
		c->umh = NULL;
	}

	cnameclose(c->name);
	c->name = cname;

	cclose(*cp);
	*cp = c;
	if (nerror)
		*nerror = 0;
	return 0;
}

/*
 * c is a mounted non-creatable directory.  find a creatable one.
 */
struct chan *createdir(struct chan *c, struct mhead *m)
{
	ERRSTACK(1);
	struct chan *nc;
	struct mount *f;

	rlock(&m->lock);
	if (waserror()) {
		runlock(&m->lock);
		nexterror();
	}
	for (f = m->mount; f; f = f->next) {
		if (f->mflag & MCREATE) {
			nc = cclone(f->to);
			runlock(&m->lock);
			poperror();
			cclose(c);
			return nc;
		}
	}
	error(Enocreate);
	poperror();
	return 0;
}

/*
 * In place, rewrite name to compress multiple /, eliminate ., and process ..
 */
void cleancname(struct cname *n)
{
	char *p;

	if (n->s[0] == '#') {
		p = strchr(n->s, '/');
		if (p == NULL)
			return;
		cleanname(p);

		/*
		 * The correct name is #i rather than #i/,
		 * but the correct name of #/ is #/.
		 */
		if (strcmp(p, "/") == 0 && n->s[1] != '/')
			*p = '\0';
	} else
		cleanname(n->s);
	n->len = strlen(n->s);
}

static void growparse(Elemlist * e)
{
	char **new;
	int *inew;
	enum { Delta = 8 };

	if (e->ARRAY_SIZEs % Delta == 0) {
		new = kzmalloc((e->ARRAY_SIZEs + Delta) * sizeof(char *), 0);
		memmove(new, e->elems, e->ARRAY_SIZEs * sizeof(char *));
		kfree(e->elems);
		e->elems = new;
		inew = kzmalloc((e->ARRAY_SIZEs + Delta + 1) * sizeof(int), 0);
		memmove(inew, e->off, e->ARRAY_SIZEs * sizeof(int));
		kfree(e->off);
		e->off = inew;
	}
}

/*
 * The name is known to be valid.
 * Copy the name so slashes can be overwritten.
 * An empty string will set ARRAY_SIZE=0.
 * A path ending in / or /. or /.//./ etc. will have
 * e.mustbedir = 1, so that we correctly
 * reject, e.g., "/adm/users/." when /adm/users is a file
 * rather than a directory.
 */
static void parsename(char *name, Elemlist * e)
{
	char *slash;

	kstrdup(&e->name, name);
	name = e->name;
	e->ARRAY_SIZEs = 0;
	e->elems = NULL;
	e->off = kzmalloc(sizeof(int), 0);
	e->off[0] = skipslash(name) - name;
	for (;;) {
		name = skipslash(name);
		if (*name == '\0') {
			e->mustbedir = 1;
			break;
		}
		growparse(e);

		e->elems[e->ARRAY_SIZEs++] = name;
		/* we may want to do this again some day
		   slash = utfrune(name, '/');
		 */
		slash = strchr(name, '/');
		if (slash == NULL) {
			e->off[e->ARRAY_SIZEs] = name + strlen(name) - e->name;
			e->mustbedir = 0;
			break;
		}
		e->off[e->ARRAY_SIZEs] = slash - e->name;
		*slash++ = '\0';
		name = slash;
	}
}

void *memrchr(void *va, int c, long n)
{
	uint8_t *a, *e;

	a = va;
	for (e = a + n - 1; e > a; e--)
		if (*e == c)
			return e;
	return NULL;
}

/*
 * Turn a name into a channel.
 * &name[0] is known to be a valid address.  It may be a kernel address.
 *
 * Opening with amode Aopen, Acreate, or Aremove guarantees
 * that the result will be the only reference to that particular fid.
 * This is necessary since we might pass the result to
 * devtab[].remove().
 *
 * Opening Atodir, Amount, or Aaccess does not guarantee this.
 *
 * Opening Aaccess can, under certain conditions, return a
 * correct Chan* but with an incorrect struct cname attached.
 * Since the functions that open Aaccess (sysstat, syswstat, sys_stat)
 * do not use the struct cname*, this avoids an unnecessary clone.
 */
struct chan *namec(char *aname, int amode, int omode, uint32_t perm)
{
	ERRSTACK(2);
	int n, prefix, len, t, nomount, npath;
	struct chan *c, *cnew;
	struct cname *cname;
	Elemlist e;
	struct mhead *m;
	char tmperrbuf[ERRMAX];
	int saved_errno;
	char *name;
	// Rune r;

	name = aname;
	if (name[0] == '\0')
		error("empty file name");
	validname(name, 1);

	/*
	 * Find the starting off point (the current slash, the root of
	 * a device tree, or the current dot) as well as the name to
	 * evaluate starting there.
	 */
	nomount = 0;
	switch (name[0]) {
		case '/':
			c = current->slash;
			if (!c)
				panic("no slash!");
			kref_get(&c->ref, 1);
			break;

		case '#':
			nomount = 1;
			get_cur_genbuf()[0] = '\0';
			n = 0;
			while (*name != '\0' && (*name != '/' || n < 2)) {
				if (n >= sizeof(get_cur_genbuf()) - 1)
					error(Efilename);
				get_cur_genbuf()[n++] = *name++;
			}
			get_cur_genbuf()[n] = '\0';
#if 0
			n = chartorune(&r, get_cur_genbuf() + 1) + 1;
			if (r == 'M')
				error(Enoattach);
#endif
			if (get_cur_genbuf()[1] == 'M')
				error(Enoattach);
			/*
			 *  the nodevs exceptions are
			 *  |  it only gives access to pipes you create
			 *  e  this process's environment
			 *  s  private file2chan creation space
			 *  D private secure sockets name space
			 *  a private TLS name space
			 */
			if (current->pgrp->nodevs &&
				//          (utfrune("|esDa", r) == NULL
				((strchr("|esDa", get_cur_genbuf()[1]) == NULL)
				 || (get_cur_genbuf()[1] == 's'	// || r == 's'
					 && get_cur_genbuf()[n] != '\0')))
				error(Enoattach);
			t = devno( /*r */ get_cur_genbuf()[1], 1);
			if (t == -1)
				error(Ebadsharp);
			/* genbuf + 2: skips the # and X, just the spec left */
			c = devtab[t].attach(get_cur_genbuf() + 2);
			break;

		default:
			c = current->dot;
			if (!c)
				panic("no dot!");
			kref_get(&c->ref, 1);
			break;
	}
	prefix = name - aname;

	e.name = NULL;
	e.elems = NULL;
	e.off = NULL;
	e.ARRAY_SIZEs = 0;
	if (waserror()) {
		cclose(c);
		kfree(e.name);
		kfree(e.elems);
		kfree(e.off);
//dumpmount();
		nexterror();
	}

	/*
	 * Build a list of elements in the path.
	 */
	parsename(name, &e);

	/*
	 * On create, ....
	 */
	if (amode == Acreate) {
		/* perm must have DMDIR if last element is / or /. */
		if (e.mustbedir && !(perm & DMDIR)) {
			npath = e.ARRAY_SIZEs;
			strncpy(tmperrbuf, "create without DMDIR", sizeof(tmperrbuf));
			goto NameError;
		}

		/* don't try to walk the last path element just yet. */
		if (e.ARRAY_SIZEs == 0)
			error(Eexist);
		e.ARRAY_SIZEs--;
	}

	if (walk(&c, e.elems, e.ARRAY_SIZEs, nomount, &npath) < 0) {
		if (npath < 0 || npath > e.ARRAY_SIZEs) {
			printd("namec %s walk error npath=%d\n", aname, npath);
			error("walk failed");
		}
NameError:
		if (current_errstr()[0]) {
			/* errstr is set, we'll just stick with it and error out */
			longjmp(&get_cur_errbuf()->jmpbuf, 1);
		} else {
			error("Name to chan lookup failed");
		}
		/* brho: skipping the namec custom error string business, since it hides
		 * the underlying failure.  implement this if you want the old stuff. */
#if 0
		strncpy(tmperrbuf, current->errstr, sizeof(tmperrbuf));
		len = prefix + e.off[npath];
		if (len < ERRMAX / 3 || (name = memrchr(aname, '/', len)) == NULL
			|| name == aname)
			snprintf(get_cur_genbuf(), sizeof current->genbuf, "%.*s", len,
					 aname);
		else
			snprintf(get_cur_genbuf(), sizeof current->genbuf, "...%.*s",
					 (int)(len - (name - aname)), name);
		snprintf(current->errstr, ERRMAX, "%#q %s", get_cur_genbuf(),
				 tmperrbuf);
#endif
	}

	if (e.mustbedir && !(c->qid.type & QTDIR)) {
		npath = e.ARRAY_SIZEs;
		strncpy(tmperrbuf, "not a directory", sizeof(tmperrbuf));
		goto NameError;
	}

	if (amode == Aopen && (omode & 3) == OEXEC && (c->qid.type & QTDIR)) {
		npath = e.ARRAY_SIZEs;
		error("cannot exec directory");
	}

	switch (amode) {
		case Aaccess:
			if (!nomount)
				domount(&c, NULL);
			break;

		case Abind:
			m = NULL;
			if (!nomount)
				domount(&c, &m);
			if (c->umh != NULL)
				putmhead(c->umh);
			c->umh = m;
			break;

		case Aremove:
		case Aopen:
Open:
			/* save the name; domount might change c */
			cname = c->name;
			kref_get(&cname->ref, 1);
			m = NULL;
			if (!nomount)
				domount(&c, &m);

			/* our own copy to open or remove */
			c = cunique(c);

			/* now it's our copy anyway, we can put the name back */
			cnameclose(c->name);
			c->name = cname;

			switch (amode) {
				case Aremove:
					putmhead(m);
					break;

				case Aopen:
				case Acreate:
					if (c->umh != NULL) {
						printd("cunique umh\n");
						putmhead(c->umh);
						c->umh = NULL;
					}

					/* only save the mount head if it's a multiple element union */
					if (m && m->mount && m->mount->next)
						c->umh = m;
					else
						putmhead(m);

					if (omode == OEXEC)
						c->flag &= ~CCACHE;

					c = devtab[c->type].open(c, omode & ~OCEXEC);
					/* here is where convert omode/vfs flags to c->flags */
					if (omode & O_APPEND)
						c->flag |= CAPPEND;
					// CEXEC should be in the FD, not the chan, right?
					if (omode & OCEXEC)
						c->flag |= CCEXEC;
					if (omode & ORCLOSE)
						c->flag |= CRCLOSE;
					break;
			}
			break;

		case Atodir:
			/*
			 * Directories (e.g. for cd) are left before the mount po int unused_int,
			 * so one may mount on / or . and see the effect.
			 */
			if (!(c->qid.type & QTDIR))
				error(Enotdir);
			break;

		case Amount:
			/*
			 * When mounting on an already mounted upon directory,
			 * one wants subsequent mounts to be attached to the
			 * original directory, not the replacement.  Don't domount.
			 */
			break;

		case Acreate:
			/*
			 * We've already walked all but the last element.
			 * If the last exists, try to open it OTRUNC.
			 * If omode&OEXCL is set, just give up.
			 */
			e.ARRAY_SIZEs++;
			if (walk(&c, e.elems + e.ARRAY_SIZEs - 1, 1, nomount, NULL) == 0) {
				if (omode & OEXCL)
					error(Eexist);
				omode |= OTRUNC;
				goto Open;
			}

			/*
			 * The semantics of the create(2) system call are that if the
			 * file exists and can be written, it is to be opened with truncation.
			 * On the other hand, the create(5) message fails if the file exists.
			 * If we get two create(2) calls happening simultaneously, 
			 * they might both get here and send create(5) messages, but only 
			 * one of the messages will succeed.  To provide the expected create(2)
			 * semantics, the call with the failed message needs to try the above
			 * walk again, opening for truncation.  This correctly solves the 
			 * create/create race, in the sense that any observable outcome can
			 * be explained as one happening before the other.
			 * The create/create race is quite common.  For example, it happens
			 * when two rc subshells simultaneously update the same
			 * environment variable.
			 *
			 * The implementation still admits a create/create/remove race:
			 * (A) walk to file, fails
			 * (B) walk to file, fails
			 * (A) create file, succeeds, returns 
			 * (B) create file, fails
			 * (A) remove file, succeeds, returns
			 * (B) walk to file, return failure.
			 *
			 * This is hardly as common as the create/create race, and is really
			 * not too much worse than what might happen if (B) got a hold of a
			 * file descriptor and then the file was removed -- either way (B) can't do
			 * anything with the result of the create call.  So we don't care about this race.
			 *
			 * Applications that care about more fine-grained decision of the races
			 * can use the OEXCL flag to get at the underlying create(5) semantics;
			 * by default we provide the common case.
			 *
			 * We need to stay behind the mount point in case we
			 * need to do the first walk again (should the create fail).
			 *
			 * We also need to cross the mount point and find the directory
			 * in the union in which we should be creating.
			 *
			 * The channel staying behind is c, the one moving forward is cnew.
			 */
			m = NULL;
			cnew = NULL;	/* is this assignment necessary? */
			/* discard error */
			if (!waserror()) {	/* try create */
				if (!nomount && findmount(&cnew, &m, c->type, c->dev, c->qid))
					cnew = createdir(cnew, m);
				else {
					cnew = c;
					kref_get(&cnew->ref, 1);
				}

				/*
				 * We need our own copy of the Chan because we're
				 * about to send a create, which will move it.  Once we have
				 * our own copy, we can fix the name, which might be wrong
				 * if findmount gave us a new Chan.
				 */
				cnew = cunique(cnew);
				cnameclose(cnew->name);
				cnew->name = c->name;
				kref_get(&cnew->name->ref, 1);

				devtab[cnew->type].create(cnew, e.elems[e.ARRAY_SIZEs - 1],
										  omode & ~(OEXCL | OCEXEC), perm);
				poperror();
				if (omode & O_APPEND)
					cnew->flag |= CAPPEND;
				if (omode & OCEXEC)
					cnew->flag |= CCEXEC;
				if (omode & ORCLOSE)
					cnew->flag |= CRCLOSE;
				if (m)
					putmhead(m);
				cclose(c);
				c = cnew;
				c->name = addelem(c->name, e.elems[e.ARRAY_SIZEs - 1]);
				break;
			}

			/* create failed */
			cclose(cnew);
			if (m)
				putmhead(m);
			if (omode & OEXCL)
				nexterror();	/* safe since we're in a waserror() */
			poperror();	/* matching the if(!waserror) */

			/* save error, so walk doesn't clobber our existing errstr */
			strncpy(tmperrbuf, current_errstr(), MAX_ERRSTR_LEN);
			saved_errno = get_errno();
			/* note: we depend that walk does not error */
			if (walk(&c, e.elems + e.ARRAY_SIZEs - 1, 1, nomount, NULL) < 0) {
				set_errno(saved_errno);
				error(tmperrbuf);	/* report the error we had originally */
			}
			strncpy(current_errstr(), tmperrbuf, MAX_ERRSTR_LEN);
			omode |= OTRUNC;
			goto Open;

		default:
			panic("unknown namec access %d\n", amode);
	}

	poperror();

	if (e.ARRAY_SIZEs > 0)
		strncpy(get_cur_genbuf(), e.elems[e.ARRAY_SIZEs - 1], GENBUF_SZ);
	else
		strncpy(get_cur_genbuf(), ".", GENBUF_SZ);

	kfree(e.name);
	kfree(e.elems);
	kfree(e.off);

	return c;
}

/*
 * name is valid. skip leading / and ./ as much as possible
 */
char *skipslash(char *name)
{
	while (name[0] == '/'
		   || (name[0] == '.' && (name[1] == 0 || name[1] == '/')))
		name++;
	return name;
}

char isfrog[256] = {
	 /*NUL*/ 1, 1, 1, 1, 1, 1, 1, 1,
	 /*BKS*/ 1, 1, 1, 1, 1, 1, 1, 1,
	 /*DLE*/ 1, 1, 1, 1, 1, 1, 1, 1,
	 /*CAN*/ 1, 1, 1, 1, 1, 1, 1, 1,
	['/'] 1,
	[0x7f] 1,
};

/*
 * Check that the name
 *  a) is in valid memory.
 *  b) is shorter than 2^16 bytes, so it can fit in a 9P string field.
 *  c) contains no frogs.
 * The first byte is known to be addressible by the requester, so the
 * routine works for kernel and user memory both.
 * The parameter slashok flags whether a slash character is an error
 * or a valid character.
 */
void validname(char *aname, int slashok)
{
	char *ename, *name;
	int c;

	name = aname;
	ename = memchr(name, 0, (1 << 16));

	if (ename == NULL || ename - name >= (1 << 16))
		error("name too long");

	while (*name) {
		/* all characters above '~' are ok */
		c = *(uint8_t *) name;
#if 0
		if (c >= Runeself)
			name += chartorune(&r, name);
#endif
		if (c >= 0x7f) {
			error("Akaros doesn't do UTF-8");
		} else {
			if (isfrog[c])
				if (!slashok || c != '/') {
					error("%s: %q", Ebadchar, aname);
				}
			name++;
		}
	}
}

void isdir(struct chan *c)
{
	if (c->qid.type & QTDIR)
		return;
	error(Enotdir);
}

/*
 * This is necessary because there are many
 * pointers to the top of a given mount list:
 *
 *	- the mhead in the namespace hash table
 *	- the mhead in chans returned from findmount:
 *	  used in namec and then by unionread.
 *	- the mhead in chans returned from createdir:
 *	  used in the open/create race protect, which is gone.
 *
 * The RWlock in the Mhead protects the mount list it contains.
 * The mount list is deleted when we cunmount.
 * The RWlock ensures that nothing is using the mount list at that time.
 *
 * It is okay to replace c->mh with whatever you want as 
 * long as you are sure you have a unique reference to it.
 *
 * This comment might belong somewhere else.
 */
void putmhead(struct mhead *m)
{
	if (m)
		kref_put(&m->ref);
}
