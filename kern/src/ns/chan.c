/*
 * Copyright 2013 Google Inc.
 * Copyright (c) 1989-2003 by Lucent Technologies, Bell Laboratories.
 */
#define DEBUG
#include <setjmp.h>
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
#include <fcall.h>

enum {
	PATHSLOP = 20,
	PATHMSLOP = 20,
};

struct {
	spinlock_t lock;
	int fid;
	struct chan *free;
	struct chan *list;
} chanalloc;

typedef struct Elemlist Elemlist;

struct Elemlist {
	char *aname;				/* original name */
	char *name;					/* copy of name, so '/' can be overwritten */
	int nelems;
	char **elems;
	int *off;
	int mustbedir;
	int nerror;
	int prefix;
};

static void mh_release(struct kref *kref)
{
	printd("mh release\n");
}

static void chan_release(struct kref *kref)
{
	printd("Chan release\n");
}

static void path9_release(struct kref *kref)
{
	printd("path release\n");
}

char *chanpath(struct chan *c, struct errbuf *perrbuf)
{
	if (c == NULL)
		return "<NULL chan>";
	if (c->path == NULL)
		return "<NULL path>";
	if (c->path->s == NULL)
		return "<NULL path.s>";
	return c->path->s;
}

int isdotdot(char *p, struct errbuf *perrbuf)
{
	return p[0] == '.' && p[1] == '.' && p[2] == '\0';
}

int emptystr(char *s, struct errbuf *perrbuf)
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
void kstrdup(char **p, char *s, struct errbuf *perrbuf)
{
	int n;
	char *t, *prev;

	n = strlen(s) + 1;
	/* if it's a user, we can wait for memory; if not, something's very wrong */
	t = kzmalloc(n, KMALLOC_WAIT);
	memmove(t, s, n);
	prev = *p;
	*p = t;
	kfree(prev);
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
		c = kzmalloc(sizeof(struct chan), KMALLOC_WAIT);
		spin_lock(&(&chanalloc)->lock);
		c->fid = ++chanalloc.fid;
		c->link = chanalloc.list;
		chanalloc.list = c;
		spin_unlock(&(&chanalloc)->lock);
	}

	c->dev = NULL;
	c->flag = 0;
	kref_init(&c->ref, chan_release, 1);
	c->devno = 0;
	c->offset = 0;
	c->devoffset = 0;
	c->iounit = 0;
	c->umh = 0;
	c->uri = 0;
	c->dri = 0;
	c->aux = 0;
	c->mchan = 0;
	c->mc = 0;
	c->mux = 0;
	memset(&c->mqid, 0, sizeof(c->mqid));
	c->path = 0;
	c->ismtpt = 0;

	return c;
}

static void fake_npath_release(struct kref *kref)
{
	panic("decremented npath below 1!");
}

struct kref npath[1] = { {(void *)1, fake_npath_release} };

struct path *newpath(char *s, struct errbuf *perrbuf)
{
	int i;
	struct path *p;

	p = kzmalloc(sizeof(struct path), KMALLOC_WAIT);
	i = strlen(s);
	p->len = i;
	p->alen = i + PATHSLOP;
	p->s = kzmalloc(p->alen, KMALLOC_WAIT);
	memmove(p->s, s, i + 1);
	kref_init(&p->ref, path9_release, 1);
	kref_get(npath, 1);

	/*
	 * Cannot use newpath for arbitrary names because the mtpt
	 * array will not be populated correctly.  The names #/ and / are
	 * allowed, but other names with / in them draw warnings.
	 */
	if (strchr(s, '/') && strcmp(s, "#/") != 0 && strcmp(s, "/") != 0)
		printd("newpath: %s from %#p\n", s, getcallerpc(&s), perrbuf);

	p->mlen = 1;
	p->malen = PATHMSLOP;
	p->mtpt = kzmalloc(p->malen * sizeof p->mtpt[0], KMALLOC_WAIT);
	return p;
}

static struct path *copypath(struct path *p, struct errbuf *perrbuf)
{
	int i;
	struct path *pp;

	pp = kzmalloc(sizeof(struct path), KMALLOC_WAIT);
	kref_init(&pp->ref, path9_release, 1);
	kref_get(npath, 1);
	printd("copypath %s %#p => %#p\n", p->s, p, pp);

	pp->len = p->len;
	pp->alen = p->alen;
	pp->s = kzmalloc(p->alen, KMALLOC_WAIT);
	memmove(pp->s, p->s, p->len + 1);

	pp->mlen = p->mlen;
	pp->malen = p->malen;
	pp->mtpt = kzmalloc(p->malen * sizeof pp->mtpt[0], KMALLOC_WAIT);
	for (i = 0; i < pp->mlen; i++) {
		pp->mtpt[i] = p->mtpt[i];
		if (pp->mtpt[i])
			kref_get(&pp->mtpt[i]->ref, 1);
	}

	return pp;
}

void pathclose(struct path *p, struct errbuf *perrbuf)
{
	int i;

	if (p == NULL)
		return;
//XXX
	printd("pathclose %#p %s ref=%d =>", p, p->s, p->ref);
	for (i = 0; i < p->mlen; i++)
		printd(" %#p", p->mtpt[i]);
	printd("\n");

	if (!kref_put(&p->ref))
		return;
	kref_put(npath);
	kfree(p->s);
	for (i = 0; i < p->mlen; i++)
		if (p->mtpt[i])
			cclose(p->mtpt[i], perrbuf);
	kfree(p->mtpt);
	kfree(p);
}

/*
 * In place, rewrite name to compress multiple /, eliminate ., and process ..
 * (Really only called to remove a trailing .. that has been added.
 * Otherwise would need to update n->mtpt as well.)
 */
static void fixdotdotname(struct path *p, struct errbuf *perrbuf)
{
	char *r;

	if (p->s[0] == '#') {
		r = strchr(p->s, '/');
		if (r == NULL)
			return;
		cleanname(r);

		/*
		 * The correct name is #i rather than #i/,
		 * but the correct name of #/ is #/.
		 */
		if (strcmp(r, "/") == 0 && p->s[1] != '/')
			*r = '\0';
	} else
		cleanname(p->s);
	p->len = strlen(p->s);
}

static struct path *uniquepath(struct path *p, struct errbuf *perrbuf)
{
	struct path *new;

	if (kref_refcnt(&p->ref) > 1) {
		/* copy on write */
		new = copypath(p, perrbuf);
		pathclose(p, perrbuf);
		p = new;
	}
	return p;
}

static struct path *addelem(struct path *p, char *s, struct chan *from,
							struct errbuf *perrbuf)
{
	char *t;
	int a, i;
	struct chan *c, **tt;

	if (s[0] == '.' && s[1] == '\0')
		return p;

	p = uniquepath(p, perrbuf);

	i = strlen(s);
	if (p->len + 1 + i + 1 > p->alen) {
		a = p->len + 1 + i + 1 + PATHSLOP;
		t = kzmalloc(a, KMALLOC_WAIT);
		memmove(t, p->s, p->len + 1);
		kfree(p->s);
		p->s = t;
		p->alen = a;
	}
	/* don't insert extra slash if one is present */
	if (p->len > 0 && p->s[p->len - 1] != '/' && s[0] != '/')
		p->s[p->len++] = '/';
	memmove(p->s + p->len, s, i + 1);
	p->len += i;
	if (isdotdot(s, perrbuf)) {
		fixdotdotname(p, perrbuf);
		printd("addelem %s .. => rm %#p\n", p->s, p->mtpt[p->mlen - 1]);
		if (p->mlen > 1 && (c = p->mtpt[--p->mlen])) {
			p->mtpt[p->mlen] = NULL;
			cclose(c, perrbuf);
		}
	} else {
		if (p->mlen >= p->malen) {
			p->malen = p->mlen + 1 + PATHMSLOP;
			tt = kzmalloc(p->malen * sizeof tt[0], KMALLOC_WAIT);
			memmove(tt, p->mtpt, p->mlen * sizeof tt[0]);
			kfree(p->mtpt);
			p->mtpt = tt;
		}
		printd("addelem %s %s => add %#p\n", p->s, s, from);
		p->mtpt[p->mlen++] = from;
		if (from)
			kref_get(&from->ref, 1);
	}
	return p;
}

void chanfree(struct chan *c, struct errbuf *perrbuf)
{
	c->flag = CFREE;

	if (c->dirrock != NULL) {
		kfree(c->dirrock);
		c->dirrock = 0;
		c->nrock = 0;
		c->mrock = 0;
	}
	if (c->umh != NULL) {
		putmhead(c->umh, perrbuf);
		c->umh = NULL;
	}
	if (c->umc != NULL) {
		cclose(c->umc, perrbuf);
		c->umc = NULL;
	}
	if (c->mux != NULL) {
		//muxclose(c->mux, perrbuf);
		c->mux = NULL;
	}
	if (c->mchan != NULL) {
		cclose(c->mchan, perrbuf);
		c->mchan = NULL;
	}

	if (c->dev != NULL) {	//XDYNX
		//devtabdecr(c->dev);
		c->dev = NULL;
	}

	pathclose(c->path, perrbuf);
	c->path = NULL;

	spin_lock(&(&chanalloc)->lock);
	c->next = chanalloc.free;
	chanalloc.free = c;
	spin_unlock(&(&chanalloc)->lock);
}

void cclose(struct chan *c, struct errbuf *perrbuf)
{
	ERRSTACK(4);

	if (c->flag & CFREE)
		panic("cclose FREE %#p", getcallerpc(&c));

	printd("cclose %#p name=%s ref=%d\n", c, c->path->s, c->ref);
	if (!kref_put(&c->ref))
		return;

	printd("cclose REALLY close\n");
	if (!waserror()) {
		if (c->dev != NULL)	//XDYNX
			c->dev->close(c, perrbuf);
	}
	chanfree(c, perrbuf);
}

#if 0
some other time.
/*
 * Queue a chan to be closed by one of the clunk procs.
 */
	struct {
	struct chan *head;
	struct chan *tail;
	int nqueued;
	int nclosed;
	spinlock_t l;
	Qspinlock_t lock q;
	Rendez r;
} clunkq;

static void closeproc(void *, struct errbuf *perrbuf);

void ccloseq(struct chan *c, struct errbuf *perrbuf)
{
	if (c->flag & CFREE)
		panic("ccloseq %#p", getcallerpc(&c));

	printd("ccloseq %#p name=%s ref=%d\n", c, c->path->s, c->ref);

	if (!kref_put(&c->ref))
		return;

	spin_lock(&(&clunkq.l)->lock);
	clunkq.nqueued++;
	c->next = NULL;
	if (clunkq.head)
		clunkq.tail->next = c;
	else
		clunkq.head = c;
	clunkq.tail = c;
	spin_unlock(&(&clunkq.l)->lock);

	if (!wakeup(&clunkq.r, perrbuf))
		kproc("closeproc", closeproc, NULL, perrbuf);
}

static int clunkwork(void *, struct errbuf *perrbuf)
{
	return clunkq.head != NULL;
}

static void closeproc(void *, struct errbuf *perrbuf)
{
	struct chan *c;

	for (;;) {
		qlock(&clunkq.q, perrbuf);
		if (clunkq.head == NULL) {
			if (!waserror()) {
				tsleep(&clunkq.r, clunkwork, NULL, 5000, perrbuf);
			}
			if (clunkq.head == NULL) {
				qunlock(&clunkq.q, perrbuf);
				pexit("no work", 1, perrbuf);
			}
		}
		spin_lock(&(&clunkq.l)->lock);
		c = clunkq.head;
		clunkq.head = c->next;
		clunkq.nclosed++;
		spin_unlock(&(&clunkq.l)->lock);
		qunlock(&clunkq.q, perrbuf);
		if (!waserror()) {
			if (c->dev != NULL)	//XDYNX
				c->dev->close(c);
		}
		chanfree(c, perrbuf);
	}
}
#endif
/*
 * Make sure we have the only copy of c.  (Copy on write.)
 */
struct chan *cunique(struct chan *c, struct errbuf *perrbuf)
{
	struct chan *nc;

	if (kref_refcnt(&c->ref) != 1) {
		nc = cclone(c, perrbuf);
		cclose(c, perrbuf);
		c = nc;
	}

	return c;
}

int eqqid(struct qid a, struct qid b, struct errbuf *perrbuf)
{
	return a.path == b.path && a.vers == b.vers;
}

static int
eqchan(struct chan *a, struct chan *b, int skipvers, struct errbuf *perrbuf)
{
	if (a->qid.path != b->qid.path)
		return 0;
	if (!skipvers && a->qid.vers != b->qid.vers)
		return 0;
	if (a->dev->dc != b->dev->dc)
		return 0;
	if (a->devno != b->devno)
		return 0;
	return 1;
}

int
eqchanddq(struct chan *c, int dc, unsigned int devno, struct qid qid,
		  int skipvers, struct errbuf *perrbuf)
{
	if (c->qid.path != qid.path)
		return 0;
	if (!skipvers && c->qid.vers != qid.vers)
		return 0;
	if (c->dev->dc != dc)
		return 0;
	if (c->devno != devno)
		return 0;
	return 1;
}

struct mhead *newmhead(struct chan *from, struct errbuf *perrbuf)
{
	struct mhead *mh;

	mh = kzmalloc(sizeof(struct mhead), KMALLOC_WAIT);
	kref_init(&mh->ref, mh_release, 1);
	mh->from = from;
	kref_get(&from->ref, 1);
	return mh;
}

int
cmount(struct chan **newp, struct chan *old, int flag, char *spec,
	   struct errbuf *perrbuf)
{
	ERRSTACK(4);

	int order, flg;
	struct chan *new;
	struct mhead *mhead, **l, *mh;
	struct mount *nm, *f, *um, **h;
	struct pgrp *pg;

	if (QTDIR & (old->qid.type ^ (*newp)->qid.type))
		error(Emount);

	if (old->umh)
		printd("cmount: unexpected umh, caller %#p\n", getcallerpc(&newp),
			   perrbuf);

	order = flag & MORDER;

	if (!(old->qid.type & QTDIR) && order != MREPL)
		error(Emount);

	new = *newp;
	mh = new->umh;

	/*
	 * Not allowed to bind when the old directory is itself a union.
	 * (Maybe it should be allowed, but I don't see what the semantics
	 * would be.)
	 *
	 * We need to check mh->mount->next to tell unions apart from
	 * simple mount points, so that things like
	 *  mount -c fd /root
	 *  bind -c /root /
	 * work.
	 *
	 * The check of mount->mflag allows things like
	 *  mount fd /root
	 *  bind -c /root /
	 *
	 * This is far more complicated than it should be, but I don't
	 * see an easier way at the moment.
	 */
	if ((flag & MCREATE) && mh && mh->mount
		&& (mh->mount->next || !(mh->mount->mflag & MCREATE)))
		error(Emount);
	pg = current->pgrp;
	wlock(&pg->ns);

	l = &MOUNTH(pg, old->qid);
	for (mhead = *l; mhead; mhead = mhead->hash) {
		if (eqchan(mhead->from, old, 1, perrbuf))
			break;
		l = &mhead->hash;
	}

	if (mhead == NULL) {
		/*
		 *  nothing mounted here yet.  create a mount
		 *  head and add to the hash table.
		 */
		mhead = newmhead(old, perrbuf);
		*l = mhead;

		/*
		 *  if this is a union mount, add the old
		 *  node to the mount chain.
		 */
		if (order != MREPL)
			mhead->mount = newmount(mhead, old, 0, 0, perrbuf);
	}
	wlock(&mhead->lock);

	if (waserror()) {
		wunlock(&mhead->lock);
		nexterror();
	}
	wunlock(&pg->ns);

	nm = newmount(mhead, new, flag, spec, perrbuf);
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
			f = newmount(mhead, um->to, flg, um->spec, perrbuf);
			*h = f;
			h = &f->next;
		}
	}

	if (mhead->mount && order == MREPL) {
		mountfree(mhead->mount, perrbuf);
		mhead->mount = 0;
	}

	if (flag & MCREATE)
		nm->mflag |= MCREATE;

	if (mhead->mount && order == MAFTER) {
		for (f = mhead->mount; f->next; f = f->next) ;
		f->next = nm;
	} else {
		for (f = nm; f->next; f = f->next) ;
		f->next = mhead->mount;
		mhead->mount = nm;
	}

	wunlock(&mhead->lock);
	return nm->mountid;
}

void cunmount(struct chan *mnt, struct chan *mounted, struct errbuf *perrbuf)
{
	struct pgrp *pg;
	struct mhead *mh, **l;
	struct mount *f, **p;

	if (mnt->umh)	/* should not happen */
		printd("cunmount newp extra umh %#p has %#p\n", mnt, mnt->umh, perrbuf);

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
	for (mh = *l; mh; mh = mh->hash) {
		if (eqchan(mh->from, mnt, 1, perrbuf))
			break;
		l = &mh->hash;
	}

	if (mh == 0) {
		wunlock(&pg->ns);
		error(Eunmount);
	}

	wlock(&mh->lock);
	if (mounted == 0) {
		*l = mh->hash;
		wunlock(&pg->ns);
		mountfree(mh->mount, perrbuf);
		mh->mount = NULL;
		cclose(mh->from, perrbuf);
		wunlock(&mh->lock);
		putmhead(mh, perrbuf);
		return;
	}

	p = &mh->mount;
	for (f = *p; f; f = f->next) {
		/* BUG: Needs to be 2 pass */
		if (eqchan(f->to, mounted, 1, perrbuf) ||
			(f->to->mchan && eqchan(f->to->mchan, mounted, 1, perrbuf))) {
			*p = f->next;
			f->next = 0;
			mountfree(f, perrbuf);
			if (mh->mount == NULL) {
				*l = mh->hash;
				cclose(mh->from, perrbuf);
				wunlock(&mh->lock);
				wunlock(&pg->ns);
				putmhead(mh, perrbuf);
				return;
			}
			wunlock(&mh->lock);
			wunlock(&pg->ns);
			return;
		}
		p = &f->next;
	}
	wunlock(&mh->lock);
	wunlock(&pg->ns);
	error(Eunion);
}

struct chan *cclone(struct chan *c, struct errbuf *perrbuf)
{
	struct chan *nc;
	struct walkqid *wq;
	wq = c->dev->walk(c, NULL, NULL, 0, perrbuf);	//XDYNX?
	if (wq == NULL)
		error("clone failed");
	nc = wq->clone;
	kfree(wq);
	nc->path = c->path;
	if (c->path)
		kref_get(&c->path->ref, 1);
	return nc;
}

/* also used by sysfile.c:/^mountfix */
int
findmount(struct chan **cp, struct mhead **mp, int dc, unsigned int devno,
		  struct qid qid, struct errbuf *perrbuf)
{
	struct pgrp *pg;
	struct mhead *mh;

	pg = current->pgrp;
	rlock(&pg->ns);
	for (mh = MOUNTH(pg, qid); mh; mh = mh->hash) {
		rlock(&mh->lock);
		if (mh->from == NULL) {
			printd("mh %#p: mh->from NULL\n", mh, perrbuf);
			runlock(&mh->lock);
			continue;
		}
		if (eqchanddq(mh->from, dc, devno, qid, 1, perrbuf)) {
			runlock(&pg->ns);
			if (mp != NULL) {
				kref_get(&mh->ref, 1);
				if (*mp != NULL)
					putmhead(*mp, perrbuf);
				*mp = mh;
			}
			if (*cp != NULL)
				cclose(*cp, perrbuf);
			kref_get(&mh->mount->to->ref, 1);
			*cp = mh->mount->to;
			runlock(&mh->lock);
			return 1;
		}
		runlock(&mh->lock);
	}

	runlock(&pg->ns);
	return 0;
}

/*
 * Calls findmount but also updates path.
 */
static int
domount(struct chan **cp, struct mhead **mp, struct path **path,
		struct errbuf *perrbuf)
{
	struct chan **lc;
	struct path *p;

	if (findmount(cp, mp, (*cp)->dev->dc, (*cp)->devno, (*cp)->qid, perrbuf) ==
		0)
		return 0;

	if (path) {
		p = *path;
		p = uniquepath(p, perrbuf);
		if (p->mlen <= 0) {
			printd("domount: path %s has mlen==%d\n", p->s, p->mlen, perrbuf);
		} else {
			lc = &p->mtpt[p->mlen - 1];
			printd("domount %#p %s => add %#p (was %#p)\n",
				   p, p->s, (*mp)->from, p->mtpt[p->mlen - 1]);
			kref_get(&(*mp)->from->ref, 1);
			if (*lc)
				cclose(*lc, perrbuf);
			*lc = (*mp)->from;
		}
		*path = p;
	}
	return 1;
}

/*
 * If c is the right-hand-side of a mount point, returns the left hand side.
 * struct changes name to reflect the fact that we've uncrossed the mountpoint,
 * so name had better be ours to change!
 */
static struct chan *undomount(struct chan *c, struct path *path,
							  struct errbuf *perrbuf)
{
	struct chan *nc;

	if (kref_refcnt(&path->ref) != 1 || path->mlen == 0)
		printd("undomount: path %s ref %d mlen %d caller %#p\n",
			   path->s, path->ref, path->mlen, getcallerpc(&c), perrbuf);
	if (path->mlen > 0 && (nc = path->mtpt[path->mlen - 1]) != NULL) {
		printd("undomount %#p %s => remove %p\n", path, path->s, nc);
		cclose(c, perrbuf);
		path->mtpt[path->mlen - 1] = NULL;
		c = nc;
	}
	return c;
}

/*
 * Call dev walk but catch errors.
 */
static struct walkqid *ewalk(struct chan *c, struct chan *nc, char **name,
							 int nname, struct errbuf *perrbuf)
{
	ERRSTACK(3);

	struct walkqid *wq;

	if (waserror())
		return NULL;
	wq = c->dev->walk(c, nc, name, nname, perrbuf);
	return wq;
}

/*
 * Either walks all the way or not at all.  No partial results in *cp.
 * *nerror is the number of names to display in an error message.
 */
static char Edoesnotexist[] = "does not exist";
int
walk(struct chan **cp, char **names, int nnames, int nomount, int *nerror,
	 struct errbuf *perrbuf)
{
	int dc, devno, didmount, dotdot, i, n, nhave, ntry;
	struct chan *c, *nc, *mtpt;
	struct path *path;
	struct mhead *mh, *nmh;
	struct mount *f;
	struct walkqid *wq;

	c = *cp;
	kref_get(&c->ref, 1);
	path = c->path;
	kref_get(&path->ref, 1);
	mh = NULL;

	/*
	 * While we haven't gotten all the way down the path:
	 *    1. step through a mount point, if any
	 *    2. send a walk request for initial dotdot or initial prefix without dotdot
	 *    3. move to the first mountpoint along the way.
	 *    4. repeat.
	 *
	 * Each time through the loop:
	 *
	 *  If didmount==0, c is on the undomount side of the mount point.
	 *  If didmount==1, c is on the domount side of the mount point.
	 *  Either way, c's full path is path.
	 */
	didmount = 0;
	for (nhave = 0; nhave < nnames; nhave += n) {
		if (!(c->qid.type & QTDIR)) {
			if (nerror)
				*nerror = nhave;
			pathclose(path, perrbuf);
			cclose(c, perrbuf);
			set_errstr(Enotdir);
			if (mh != NULL)
				putmhead(mh, perrbuf);
			return -1;
		}
		ntry = nnames - nhave;
		if (ntry > MAXWELEM)
			ntry = MAXWELEM;
		dotdot = 0;
		for (i = 0; i < ntry; i++) {
			if (isdotdot(names[nhave + i], perrbuf)) {
				if (i == 0) {
					dotdot = 1;
					ntry = 1;
				} else
					ntry = i;
				break;
			}
		}

		if (!dotdot && !nomount && !didmount)
			domount(&c, &mh, &path, perrbuf);

		dc = c->dev->dc;
		devno = c->devno;

		if ((wq = ewalk(c, NULL, names + nhave, ntry, perrbuf)) == NULL) {
			/* try a union mount, if any */
			if (mh && !nomount) {
				/*
				 * mh->mount->to == c, so start at mh->mount->next
				 */
				rlock(&mh->lock);
				if (mh->mount) {
					for (f = mh->mount->next; f != NULL; f = f->next) {
						if ((wq =
							 ewalk(f->to, NULL, names + nhave, ntry,
								   perrbuf)) != NULL) {
							dc = f->to->dev->dc;
							devno = f->to->devno;
							break;
						}
					}
				}
				runlock(&mh->lock);
			}
			if (wq == NULL) {
				cclose(c, perrbuf);
				pathclose(path, perrbuf);
				if (nerror)
					*nerror = nhave + 1;
				if (mh != NULL)
					putmhead(mh, perrbuf);
				return -1;
			}
		}

		nmh = NULL;
		didmount = 0;
		if (dotdot) {
			assert(wq->nqid == 1);
			assert(wq->clone != NULL);

			path = addelem(path, "..", NULL, perrbuf);
			nc = undomount(wq->clone, path, perrbuf);
			n = 1;
		} else {
			nc = NULL;
			if (!nomount) {
				for (i = 0; i < wq->nqid && i < ntry - 1; i++) {
					if (findmount(&nc, &nmh, dc, devno, wq->qid[i], perrbuf)) {
						didmount = 1;
						break;
					}
				}
			}
			if (nc == NULL) {	/* no mount points along path */
				if (wq->clone == NULL) {
					cclose(c, perrbuf);
					pathclose(path, perrbuf);
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
						putmhead(mh, perrbuf);
					return -1;
				}
				n = wq->nqid;
				nc = wq->clone;
			} else {	/* stopped early, at a mount point */
				didmount = 1;
				if (wq->clone != NULL) {
					cclose(wq->clone, perrbuf);
					wq->clone = NULL;
				}
				n = i + 1;
			}
			for (i = 0; i < n; i++) {
				mtpt = NULL;
				if (i == n - 1 && nmh)
					mtpt = nmh->from;
				path = addelem(path, names[nhave + i], mtpt, perrbuf);
			}
		}
		cclose(c, perrbuf);
		c = nc;
		putmhead(mh, perrbuf);
		mh = nmh;
		kfree(wq);
	}

	putmhead(mh, perrbuf);
	c = cunique(c, perrbuf);

	if (c->umh != NULL) {	//BUG
		printd("walk umh\n", perrbuf);
		putmhead(c->umh, perrbuf);
		c->umh = NULL;
	}

	pathclose(c->path, perrbuf);
	c->path = path;

	cclose(*cp, perrbuf);
	*cp = c;
	if (nerror)
		*nerror = nhave;
	return 0;
}

/*
 * c is a mounted non-creatable directory.  find a creatable one.
 */
struct chan *createdir(struct chan *c, struct mhead *mh, struct errbuf *perrbuf)
{
	ERRSTACK(3);

	struct chan *nc;
	struct mount *f;

	rlock(&mh->lock);

	if (waserror()) {
		runlock(&mh->lock);
		nexterror();
	}
	for (f = mh->mount; f; f = f->next) {
		if (f->mflag & MCREATE) {
			nc = cclone(f->to, perrbuf);
			runlock(&mh->lock);
			cclose(c, perrbuf);
			return nc;
		}
	}
	error(Enocreate);
	return 0;
}

static void growparse(Elemlist * e, struct errbuf *perrbuf)
{
	char **new;
	int *inew;
	enum { Delta = 8 };

	if (e->nelems % Delta == 0) {
		new = kzmalloc((e->nelems + Delta) * sizeof(char *), KMALLOC_WAIT);
		memmove(new, e->elems, e->nelems * sizeof(char *));
		kfree(e->elems);
		e->elems = new;
		inew = kzmalloc((e->nelems + Delta + 1) * sizeof(int), KMALLOC_WAIT);
		memmove(inew, e->off, (e->nelems + 1) * sizeof(int));
		kfree(e->off);
		e->off = inew;
	}
}

/*
 * The name is known to be valid.
 * Copy the name so slashes can be overwritten.
 * An empty string will set nelem=0.
 * A path ending in / or /. or /.//./ etc. will have
 * e.mustbedir = 1, so that we correctly
 * reject, e.g., "/adm/users/." when /adm/users is a file
 * rather than a directory.
 * No UTF. 
 */
static void parsename(char *aname, Elemlist * e, struct errbuf *perrbuf)
{
	char *name, *slash;

	kstrdup(&e->name, aname, perrbuf);
	name = e->name;
	e->nelems = 0;
	e->elems = NULL;
	e->off = kzmalloc(sizeof(int), KMALLOC_WAIT);
	e->off[0] = skipslash(name) - name;
	for (;;) {
		name = skipslash(name);
		if (*name == '\0') {
			e->off[e->nelems] = name + strlen(name) - e->name;
			e->mustbedir = 1;
			break;
		}
		growparse(e, perrbuf);
		e->elems[e->nelems++] = name;
		slash = memchr(name, '/', strlen(name));
		if (slash == NULL) {
			e->off[e->nelems] = name + strlen(name) - e->name;
			e->mustbedir = 0;
			break;
		}
		e->off[e->nelems] = slash - e->name;
		*slash++ = '\0';
		name = slash;
	}

	if (2 > 1) {
		int i;

		printd("parsename %s:", e->name);
		for (i = 0; i <= e->nelems; i++)
			printd(" %d", e->off[i]);
		printd("\n");
	}
}

static void *memrchr(void *va, int c, long n)
{
	unsigned int *a, *e;

	a = va;
	for (e = a + n - 1; e > a; e--)
		if (*e == c)
			return e;
	return NULL;
}

static void namelenerror(char *aname, int len, char *err, struct errbuf *perrbuf)
{
	ERRSTACK(1);
	char *ename, *name, *next;
	int i, errlen;

	/*
	 * If the name is short enough, just use the whole thing.
	 */
	errlen = strlen(err);
	if (len < ERRMAX / 3 || len + errlen < 2 * ERRMAX / 3)
		snprintf(current->genbuf, sizeof current->genbuf, "%.*s", len, aname);
	else {
		/*
		 * Print a suffix of the name, but try to get a little info.
		 */
		ename = aname + len;
		next = ename;
		do {
			name = next;
			next = memrchr(aname, '/', name - aname);
			if (next == NULL)
				next = aname;
			len = ename - next;
		} while (len < ERRMAX / 3 || len + errlen < 2 * ERRMAX / 3);

		/*
		 * If the name is ridiculously long, chop it.
		 */
		if (name == ename) {
			name = ename - ERRMAX / 4;
			if (name <= aname)
				panic("bad math in namelenerror");
			/* walk out of current UTF sequence */
			for (i = 0; (*name & 0xC0) == 0x80 && i < UTFmax; i++)
				name++;
		}
		snprintf(current->genbuf, sizeof current->genbuf, "...%.*s",
				 strlen(name), name);
	}
	snprintf(current_errstr(), MAX_ERRSTR_LEN, "%#q %s", current->genbuf, err);
	nexterror();
}

void nameerror(char *name, char *err, struct errbuf *perrbuf)
{
	namelenerror(name, strlen(name), err, perrbuf);
}

/*
 * Turn a name into a channel.
 * &name[0] is known to be a valid address.  It may be a kernel address.
 *
 * Opening with amode Aopen, Acreate, Aremove, or Aaccess guarantees
 * that the result will be the only reference to that particular fid.
 * This is necessary since we might pass the result to
 * devtab[]->remove().
 *
 * Opening Atodir or Amount does not guarantee this.
 *
 * Under certain circumstances, opening Aaccess will cause
 * an unnecessary clone in order to get a cunique struct chan so it
 * can attach the correct name.  Sysstat and sys_stat need the
 * correct name so they can rewrite the stat info.
 */
struct chan *namec(char *aname, int amode, int omode, int perm,
				   struct errbuf *perrbuf)
{
	ERRSTACK(4);
	int len, n, nomount;
	struct chan *c, *cnew;
	struct path *path;
	Elemlist e;
	struct mhead *mh;
	char tmperrbuf[ERRMAX];	/* ERRMAX still, for namelenerror */
	char *name;
	struct dev *dev;
	printd("namec name %s\n", aname);
	if (aname[0] == '\0')
		error("empty file name");
	aname = validnamedup(aname, 1, perrbuf);

	if (waserror()) {
		kfree(aname);
		nexterror();
	}
	printd("namec %s %d %d\n", aname, amode, omode);
	name = aname;

	/*
	 * Find the starting off point (the current slash, the root of
	 * a device tree, or the current dot) as well as the name to
	 * evaluate starting there.
	 */
	nomount = 0;
	switch (name[0]) {
		case '/':
			error("not yet");
			c = current->slash;
			/* TODO: we still have scenarios where there is no current / */
			if (!c)
				error(Enotdir);
			kref_get(&c->ref, 1);
			break;

		case '#':
			nomount = 1;
			current->genbuf[0] = '\0';
			n = 0;
			while (*name != '\0' && (*name != '/' || n < 2)) {
				if (n >= sizeof(current->genbuf) - 1)
					error(Efilename);
				current->genbuf[n++] = *name++;
			}
			current->genbuf[n] = '\0';
			/*
			 *  noattach is sandboxing.
			 *
			 *  the OK exceptions are:
			 *  |  it only gives access to pipes you create
			 *  d  this process's file descriptors
			 *  e  this process's environment
			 *  the iffy exceptions are:
			 *  c  time and pid, but also cons and consctl
			 *  p  control of your own processes (and unfortunately
			 *     any others left unprotected)
			 */
			/* actually / is caught by parsing earlier */
			if (current->genbuf[1] == 'M')
				error(Enoattach);
			if (current->pgrp->noattach) {
				if (current->genbuf[1] != '|' &&
					current->genbuf[1] != 'e' &&
					current->genbuf[1] != 'c' && current->genbuf[1] != 'p')
					error(Enoattach);
			}
			dev = devtabget(current->genbuf[1], 1, perrbuf);	//XDYNX
			if (dev == NULL)
				error(Ebadsharp);
			//if(waserror()){
			//  devtabdecr(dev);
			//  nexterror();
			//}
			c = dev->attach(current->genbuf + 2, perrbuf);
			//poperror();
			//devtabdecr(dev);
			break;

		default:
			error("No current->dot yet");
			c = current->dot;
			kref_get(&c->ref, 1);
			break;
	}

	e.aname = aname;
	e.prefix = name - aname;
	e.name = NULL;
	e.elems = NULL;
	e.off = NULL;
	e.nelems = 0;
	e.nerror = 0;
	if (waserror()) {
		cclose(c, perrbuf);
		kfree(e.name);
		kfree(e.elems);
		/*
		 * Prepare nice error, showing first e.nerror elements of name.
		 */
		if (e.nerror == 0)
			nexterror();
		strncpy(tmperrbuf, current_errstr(), MAX_ERRSTR_LEN);
		if (e.off[e.nerror] == 0)
			printd("nerror=%d but off=%d\n",
				   e.nerror, e.off[e.nerror], perrbuf);
		if (2 > 0) {
			printd("showing %d+%d/%d (of %d) of %s (%d %d)\n",
				   e.prefix, e.off[e.nerror], e.nerror,
				   e.nelems, aname, e.off[0], e.off[1]);
		}
		len = e.prefix + e.off[e.nerror];
		kfree(e.off);
		namelenerror(aname, len, tmperrbuf, perrbuf);
	}

	/*
	 * Build a list of elements in the name.
	 */
	parsename(name, &e, perrbuf);

	/*
	 * On create, ....
	 */
	if (amode == Acreate) {
		/* perm must have DMDIR if last element is / or /. */
		if (e.mustbedir && !(perm & DMDIR)) {
			e.nerror = e.nelems;
			error("create without DMDIR");
		}

		/* don't try to walk the last path element just yet. */
		if (e.nelems == 0)
			error(Eexist);
		e.nelems--;
	}
	if (walk(&c, e.elems, e.nelems, nomount, &e.nerror, perrbuf) < 0) {
		if (e.nerror < 0 || e.nerror > e.nelems) {
			printd("namec %s walk error nerror=%d\n", aname, e.nerror, perrbuf);
			e.nerror = 0;
		}
		nexterror();
	}

	if (e.mustbedir && !(c->qid.type & QTDIR))
		error("not a directory");

	if (amode == Aopen && (omode & 3) == OEXEC && (c->qid.type & QTDIR))
		error("cannot exec directory");

	switch (amode) {
		case Abind:
			/* no need to maintain path - cannot dotdot an Abind */
			mh = NULL;
			if (!nomount)
				domount(&c, &mh, NULL, perrbuf);
			if (c->umh != NULL)
				putmhead(c->umh, perrbuf);
			c->umh = mh;
			break;

		case Aaccess:
		case Aremove:
		case Aopen:
Open:
			/* save&update the name; domount might change c */
			path = c->path;
			kref_get(&path->ref, 1);
			mh = NULL;
			if (!nomount)
				domount(&c, &mh, &path, perrbuf);

			/* our own copy to open or remove */
			c = cunique(c, perrbuf);

			/* now it's our copy anyway, we can put the name back */
			pathclose(c->path, perrbuf);
			c->path = path;

			/* record whether c is on a mount point */
			c->ismtpt = mh != NULL;

			switch (amode) {
				case Aaccess:
				case Aremove:
					putmhead(mh, perrbuf);
					break;

				case Aopen:
				case Acreate:
					if (c->umh != NULL) {
						printd("cunique umh Open\n", perrbuf);
						putmhead(c->umh, perrbuf);
						c->umh = NULL;
					}
					/* only save the mount head if it's a multiple element union */
					if (mh && mh->mount && mh->mount->next)
						c->umh = mh;
					else
						putmhead(mh, perrbuf);

					if (omode == OEXEC)
						c->flag &= ~CCACHE;

//open:                         //XDYNX
// get dev
// open
// if no error and read/write
// then fill in c->dev and
// don't put
					c = c->dev->open(c, omode & ~OCEXEC, perrbuf);

					if (omode & OCEXEC)
						c->flag |= CCEXEC;
					if (omode & ORCLOSE)
						c->flag |= CRCLOSE;
					break;
			}
			break;

		case Atodir:
			/*
			 * Directories (e.g. for cd) are left before the mount point,
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
			e.nelems++;
			e.nerror++;
			if (walk(&c, e.elems + e.nelems - 1, 1, nomount, NULL, perrbuf) ==
				0) {
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
			mh = NULL;
			cnew = NULL;	/* is this assignment necessary? */
			if (!waserror()) {	/* try create */
				if (!nomount
					&& findmount(&cnew, &mh, c->dev->dc, c->devno, c->qid,
								 perrbuf))
					cnew = createdir(cnew, mh, perrbuf);
				else {
					cnew = c;
					kref_get(&cnew->ref, 1);
				}

				/*
				 * We need our own copy of the struct chan because we're
				 * about to send a create, which will move it.  Once we have
				 * our own copy, we can fix the name, which might be wrong
				 * if findmount gave us a new struct chan.
				 */
				cnew = cunique(cnew, perrbuf);
				pathclose(cnew->path, perrbuf);
				cnew->path = c->path;
				kref_get(&cnew->path->ref, 1);

//create:                       //XDYNX
// like open regarding read/write?

				cnew->dev->create(cnew, e.elems[e.nelems - 1],
								  omode & ~(OEXCL | OCEXEC), perm, perrbuf);
				if (omode & OCEXEC)
					cnew->flag |= CCEXEC;
				if (omode & ORCLOSE)
					cnew->flag |= CRCLOSE;
				if (mh)
					putmhead(mh, perrbuf);
				cclose(c, perrbuf);
				c = cnew;
				c->path =
					addelem(c->path, e.elems[e.nelems - 1], NULL, perrbuf);
				break;
			}
			/* create failed */
			cclose(cnew, perrbuf);
			if (mh)
				putmhead(mh, perrbuf);
			if (omode & OEXCL)
				nexterror();
			/* save error, so walk doesn't clobber our existing errstr */
			strncpy(tmperrbuf, current_errstr(), MAX_ERRSTR_LEN);
			/* note: we depend that walk does not error */
			if (walk(&c, e.elems + e.nelems - 1, 1, nomount, NULL, perrbuf)
			    < 0) {
				error(tmperrbuf);	/* report the error we had originally */
			}
			strncpy(current_errstr(), tmperrbuf, MAX_ERRSTR_LEN);
			omode |= OTRUNC;
			goto Open;

		default:
			panic("unknown namec access %d", amode, perrbuf);
	}

	/* place final element in genbuf for e.g. exec */
	if (e.nelems > 0)
		kstrcpy(current->genbuf, e.elems[e.nelems - 1], sizeof current->genbuf);
	else
		kstrcpy(current->genbuf, ".", sizeof(current->genbuf));
	kfree(e.name);
	kfree(e.elems);
	kfree(e.off);	/* e c */
	kfree(aname);	/* aname */

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

/* note: calls error. */
void validname(char *aname, int slashok, struct errbuf *perrbuf)
{
	validname0(aname, slashok, 0, getcallerpc(&aname), perrbuf);
}

char *validnamedup(char *aname, int slashok, struct errbuf *perrbuf)
{
	return validname0(aname, slashok, 1, getcallerpc(&aname), perrbuf);
}

void isdir(struct chan *c, struct errbuf *perrbuf)
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
 * The RWlock in the struct mhead protects the mount list it contains.
 * The mount list is deleted when we cunmount.
 * The RWlock ensures that nothing is using the mount list at that time.
 *
 * It is okay to replace c->mh with whatever you want as
 * long as you are sure you have a unique reference to it.
 *
 * This comment might belong somewhere else.
 */
void putmhead(struct mhead *mh, struct errbuf *perrbuf)
{
	if (mh && kref_put(&mh->ref) == 0) {
		mh->mount = (struct mount *)0xCafeBeef;
		kfree(mh);
	}
}
