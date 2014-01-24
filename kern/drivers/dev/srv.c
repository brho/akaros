/* Copyright (c) 2014 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * #s (srv) - a chan sharing service.  This was originally based off the Inferno
 * #s, but it's been completely rewritten to act like what I remember the plan9
 * one to be like.
 *
 * I'm trying a style where we hang items off c->aux, specific to each chan.
 * Instead of looking up via qid.path, we just look at the c->aux for our
 * struct.  This has been a pain in the ass, and might have issues still.
 *
 * basically, whenever we gen a response, the chan will now have the aux for
 * that response.  though we don't change the chan's qid (devwalk will do that
 * on a successful run).
 *
 * one consequence of the refcnt style is that so long as an FD is open, it'll
 * hold the chan and the srvfile in memory.  even if the file is removed from
 * #s, it can still be read and written.  removal only prevents future walks.
 *
 * i've seen other devices do the chan->aux thing, but not like this.  those
 * other ones had aux be for a device instance (like pipe - every attach creates
 * its own pipe instance). */

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
#include <sys/queue.h>

#define Qtopdir			1
#define Qsrvfile		2

struct srvfile {
	TAILQ_ENTRY(srvfile) link;
	char *name;
	struct chan *chan;
	struct kref ref;		/* +1 for existing on create, -1 on remove */
	bool on_list;
	char *user;
	uint32_t perm;
	atomic_t opens;			/* used for exclusive open checks */
};

struct srvfile *top_dir;
TAILQ_HEAD(srvfilelist, srvfile) srvfiles = TAILQ_HEAD_INITIALIZER(srvfiles);
/* the lock protects the list and its members.  we don't incref from a list ref
 * without the lock. (if you're on the list, we can grab a ref).  Nor do we
 * remove or mess with 'on_list' for any member without the lock. */
spinlock_t srvlock = SPINLOCK_INITIALIZER;

atomic_t nr_srvs = 0; /* debugging - concerned about leaking mem */

static void srv_release(struct kref *kref)
{
	struct srvfile *srv = container_of(kref, struct srvfile, ref);
	kfree(srv->user);
	kfree(srv->name);
	if (srv->chan)
		cclose(srv->chan);
	kfree(srv);
	atomic_dec(nr_srvs);
}

static int srvgen(struct chan *c, char *name, struct dirtab *tab,
                  int ntab, int s, struct dir *dp)
{
	struct srvfile *prev, *next;
	struct qid q;

	if (s == DEVDOTDOT) {
		/* changing whatever c->aux was to be topdir */
		kref_get(&top_dir->ref, 1);
		kref_put(&((struct srvfile*)(c->aux))->ref);
		mkqid(&q, Qtopdir, 0, QTDIR);
		devdir(c, q, "#s", 0, eve, 0555, dp);
		return 1;
	}
	/* we're genning the contents of the #s/ directory.  first time through, we
	 * just give the first item.  we have a c->aux from a previous run, though
	 * for s == 0, it's not necessarily the top_dir.
	 *
	 * while we do come in with c->aux, and it is a srvfile, it might be one
	 * that is removed from the list.  since we always add new items to the
	 * tail, if prev is still on the list, we can just return the next one.  but
	 * if we aren't on the list, we'll need to gen from scratch.
	 *
	 * This might be fucked up when we're genning and not starting from s = 0,
	 * and when prev isn't the actual previous one.  devdirread will start from
	 * other 's'es.  If prev is fucked up, we'll have to take out the on_list
	 * optimization. */
	prev = c->aux;
	spin_lock(&srvlock);
	if (s == 0) {
		next = TAILQ_FIRST(&srvfiles);
	} else if (prev->on_list) {
		assert(prev != top_dir);
		next = TAILQ_NEXT(prev, link);
	} else {
		/* fall back to the usual scan, which is normally O(n) (O(n^2) overall).
		 * though this only happens when there was some race and prev was
		 * removed, and next gen it should be O(1). */
		TAILQ_FOREACH(next, &srvfiles, link) {
			/* come in with s == 0 on the first run */
			if (s-- == 0)
				break;
		}
	}
	if (!next) {
		spin_unlock(&srvlock);
		return -1;
	}
	/* the list lock allows us to grab the ref */
	kref_get(&next->ref, 1); 	/* passing out via c->aux */
	spin_unlock(&srvlock);
	/* update c to point to our new srvfile.  this keeps the chan and its srv in
	 * sync with what we're genning.  this does suck a bit, since we'll do a
	 * lot of increfs and decrefs. */
	kref_put(&prev->ref);
	c->aux = next;		
	mkqid(&q, Qsrvfile, 0, QTFILE);
	devdir(c, q, next->name, 1/* length */, next->user, next->perm, dp);
	return 1;
}

static void __srvinit(void)
{
	top_dir = kzmalloc(sizeof(struct srvfile), KMALLOC_WAIT);
	/* kstrdup, just in case we free this later */
	kstrdup(&top_dir->name, "srv");
	kstrdup(&top_dir->user, current ? current->user : "eve");
	top_dir->perm = DMDIR | 0770;
	/* +1 for existing, should never decref this */
	kref_init(&top_dir->ref, fake_release, 1);
	atomic_set(&top_dir->opens, 0);
}

static void srvinit(void)
{
	run_once(__srvinit());
}

static struct chan *srvattach(char *spec)
{
	/* the inferno attach was pretty complicated, but
	 * we're not sure that complexity is needed. */
	struct chan *c = devattach('s', spec);
	mkqid(&c->qid, Qtopdir, 0, QTDIR);
	/* c->aux is a counted ref */
	kref_get(&top_dir->ref, 1);
	c->aux = top_dir;
	return c;
}

static struct walkqid *srvwalk(struct chan *c, struct chan *nc, char **name,
							   int nname)
{
	struct srvfile *srv = c->aux;
	assert(srv);	/* all srv chans should have an aux. */
	struct walkqid *wq = devwalk(c, nc, name, nname, 0, 0, srvgen);
	/* I don't fully understand this, but when wq and wq->clone return, we need
	 * to account for the aux (example, devip, devpipe, etc, though those differ
	 * in that they have one aux for the entire #device).
	 *
	 * i think this is because the callers of our walk track and cclose the
	 * initial chan , regardless of what we did internally.  walk() does this
	 * (via *cp).  though the main one i've run into is cclone().  callers of
	 * cclone() close both the source chan and the cloned chan.
	 *
	 * So even though our gen moved along the steps of the state machine,
	 * tracking each change of the working chan (via inc and decref), the caller
	 * has an extra copy of the original c that it'll close.  
	 *
	 * So i think we should be increffing the original c->aux, and that we
	 * already accounted for wq->clone (when we ran gen, and genned the final
	 * answer).
	 *
	 * Regarding the clone != c check: many other devs do this, and it's
	 * probably correct.  One thing: the c->aux is per chan, and the srv ref
	 * only gets put once per chan close.  If the two chans (clone and c) are
	 * the same, then there is one real chan with a chan refcnt of 2.  We don't
	 * want a refcnt of 2 on the srv, since there is really only one srv ref.
	 *
	 * Oh, and FYI, when we're called from clone, nnames == 0, and some devs
	 * (like the original inferno srv) treat that case differently. */
	if (wq && wq->clone && (wq->clone != c)) {
		assert(wq->clone->aux);	/* make sure this ran through gen */
		kref_get(&srv->ref, 1);
	}
	return wq;
}

static int srvstat(struct chan *c, uint8_t * db, int n)
{
	return devstat(c, db, n, 0, 0, srvgen);
}

static struct chan *srvopen(struct chan *c, int omode)
{
	struct srvfile *srv;
	openmode(omode);	/* used as an error checker in plan9, does little now */
	if (c->qid.type & QTDIR) {
		if (!IS_RDONLY(omode))
			error(Eisdir);
		c->mode = openmode(omode);
		c->flag |= COPEN;
		c->offset = 0;
		return c;
	}
	/* c->aux is a counted ref.  srv could be removed from the list already. */
	srv = c->aux;
	devpermcheck(srv->user, srv->perm, omode);
	/* No remove on close support yet */
	#if 0
	if (omode & ORCLOSE) {
		if (strcmp(srv->user, up->env->user) != 0)
			error(Eperm);
		else
			srv->flags |= SORCLOSE;
	}
	#endif
	if ((srv->perm & DMEXCL) && atomic_read(&srv->opens))
		error(Einuse);
	/* srv->chan is write-once, so we don't need to sync. */
	if (!srv->chan)
		error("srv file has no chan yet");
	/* this is more than just the ref - 1, since there will be refs in flight
	 * as gens work their way through the list */
	atomic_inc(&srv->opens);
	/* the magic of srv: open c, get c->srv->chan back */
	cclose(c);	/* also closes/decrefs c->aux */
	c = srv->chan;
	kref_get(&c->ref, 1);
	return c;
}

static void srvcreate(struct chan *c, char *name, int omode, uint32_t perm)
{
	struct srvfile *srv;
	srv = kzmalloc(sizeof(struct srvfile), KMALLOC_WAIT);
	kstrdup(&srv->name, name);
	kstrdup(&srv->user, current ? current->user : "eve");
	srv->perm = 0770;	/* TODO need some security thoughts */
	atomic_set(&srv->opens, 1);			/* we return it opened */
	mkqid(&c->qid, Qsrvfile, 0, QTFILE);
	/* we're switching the chan that came in from the old one to the one we
	 * created.  we're caleled from namec, where our c == cnew. */
	kref_put(&((struct srvfile*)c->aux)->ref);
	c->aux = srv;
	/* one ref for being on the list, another for c->aux */
	kref_init(&srv->ref, srv_release, 2);
	spin_lock(&srvlock);
	srv->on_list = TRUE;
	TAILQ_INSERT_TAIL(&srvfiles, srv, link);
	spin_unlock(&srvlock);
	atomic_inc(&nr_srvs);
}

static int srvwstat(struct chan *c, uint8_t * dp, int n)
{
	error("srvwstat not supported yet");
	return -1;
}

static void srvclose(struct chan *c)
{
	struct srvfile *srv = c->aux;
	atomic_dec(&srv->opens);
	kref_put(&srv->ref);
}

static void srvremove(struct chan *c)
{
	struct srvfile *srv = c->aux;

	spin_lock(&srvlock);
	/* could have a concurrent removal */
	if (srv->on_list) {
		TAILQ_REMOVE(&srvfiles, srv, link);
		srv->on_list = FALSE;
		/* for my sanity.  when closing, we should have two refs, one for being
		 * on the list, and the other for c->aux, which gets closed later. */
		assert(kref_refcnt(&srv->ref) > 1);
		kref_put(&srv->ref);	/* dropping ref from the list */
	}
	spin_unlock(&srvlock);
}

/* N.B. srvopen gives the chan back. The only 'reading' we do
 * in srv is of the top level directory.
 */
static long srvread(struct chan *c, void *va, long count, int64_t offset)
{
	return devdirread(c, va, count, 0, 0, srvgen);
}

static long srvwrite(struct chan *c, void *va, long count, int64_t offset)
{
	ERRSTACK(1);
	struct srvfile *srv;
	struct chan *new_chan;
	char *kbuf = 0;
	int fd;

	if (c->qid.type & QTDIR)
		error(Eperm);
	/* srv is refcnt'd, no fear of it disappearing */
	srv = c->aux;
	if (srv->chan)
		error("srv file already has a stored chan!");
	if (waserror()) {
		kfree(kbuf);
		nexterror();
	}
	kbuf = kmalloc(count + 1, KMALLOC_WAIT);
	strncpy(kbuf, va, count);
	kbuf[count] = 0;
	fd = strtoul(kbuf, 0, 10);
	/* the magic of srv: srv stores the chan corresponding to the fd.  -1 for
	 * mode, so we just get the chan with no checks (RDWR would work too). */
	new_chan = fdtochan(current->fgrp, fd, -1, FALSE, TRUE);
	/* fdtochan already increffed for us */
	if (!__sync_bool_compare_and_swap(&srv->chan, 0, new_chan)) {
		cclose(new_chan);
		error("srv file already has a stored chan!");
	}
	poperror();
	kfree(kbuf);
	return count;
}

struct dev srvdevtab __devtab = {
	's',
	"srv",

	devreset,
	srvinit,
	devshutdown,
	srvattach,
	srvwalk,
	srvstat,
	srvopen,
	srvcreate,
	srvclose,
	srvread,
	devbread,
	srvwrite,
	devbwrite,
	srvremove,
	srvwstat
};
