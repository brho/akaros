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

/* TODO: (ID) need a unique ID service.  These will loop around... */
static int pgrpid;
static int mountid;
#define NEXT_ID(x) (__sync_add_and_fetch(&(x), 1))

void closepgrp(struct pgrp *p)
{
	struct mhead **h, **e, *f, *next;

	wlock(&p->ns);
	p->pgrpid = -1;

	e = &p->mnthash[MNTHASH];
	for (h = p->mnthash; h < e; h++) {
		for (f = *h; f; f = next) {
			wlock(&f->lock);
			cclose(f->from);
			mountfree(f->mount);
			f->mount = NULL;
			next = f->hash;
			wunlock(&f->lock);
			putmhead(f);
		}
	}
	wunlock(&p->ns);
	cclose(p->dot);
	cclose(p->slash);
	kfree(p);
}

static void freepgrp(struct kref *k)
{
	struct pgrp *p = container_of(k, struct pgrp, ref);
	closepgrp(p);
}

struct pgrp *newpgrp(void)
{
	struct pgrp *p;

	p = kzmalloc(sizeof(struct pgrp), KMALLOC_WAIT);
	kref_init(&p->ref, freepgrp, 1);
	p->pgrpid = NEXT_ID(pgrpid);
	p->progmode = 0644;
	qlock_init(&p->debug);
	qlock_init(&p->nsh);
	return p;
}

void pgrpinsert(struct mount **order, struct mount *m)
{
	struct mount *f;

	m->order = 0;
	if (*order == 0) {
		*order = m;
		return;
	}
	for (f = *order; f; f = f->order) {
		if (m->mountid < f->mountid) {
			m->order = f;
			*order = m;
			return;
		}
		order = &f->order;
	}
	*order = m;
}

/*
 * pgrpcpy MUST preserve the mountid allocation order of the parent group
 */
void pgrpcpy(struct pgrp *to, struct pgrp *from)
{
	ERRSTACK(2);
	int i;
	struct mount *n, *m, **link, *order;
	struct mhead *f, **tom, **l, *mh;

	wlock(&from->ns);
	if (waserror()) {
		wunlock(&from->ns);
		nexterror();
	}
	order = 0;
	tom = to->mnthash;
	for (i = 0; i < MNTHASH; i++) {
		l = tom++;
		for (f = from->mnthash[i]; f; f = f->hash) {
			rlock(&f->lock);
			if (waserror()) {
				runlock(&f->lock);
				nexterror();
			}
			mh = newmhead(f->from);
			if (!mh)
				error(Enomem);
			*l = mh;
			l = &mh->hash;
			link = &mh->mount;
			for (m = f->mount; m; m = m->next) {
				n = newmount(mh, m->to, m->mflag, m->spec);
				m->copy = n;
				pgrpinsert(&order, m);
				*link = n;
				link = &n->next;
			}
			poperror();
			runlock(&f->lock);
		}
	}
	/*
	 * Allocate mount ids in the same sequence as the parent group
	 */
	/* should probably protect with a spinlock and be done with it */
	for (m = order; m; m = m->order) {
		m->copy->mountid = NEXT_ID(mountid);
	}

	to->progmode = from->progmode;
	to->slash = cclone(from->slash);
	to->dot = cclone(from->dot);
	to->nodevs = from->nodevs;

	poperror();
	wunlock(&from->ns);
}

void closefgrp(struct fgrp *f)
{
	/* TODO: look at this more carefully.  sharing fgrps might be unnecessary,
	 * due to our parallelism style. */
	/* closefgrp can't be called from proc_destroy, due to races on the fgrp.
	 * current->fgrp is a kref source, and we'd need some form of external sync
	 * to remove it, since multiple kthreads could be accessing current->fgrp.
	 * proc_free is synchronized, for instance.  we could put some sync in the
	 * fgrp, but that would require splitting the deallocation (which we do
	 * manually), and would require not having multiple procs per fgrp (which we
	 * also require).  another option would be to use RCU: clear current->fgrp,
	 * then closefgrp after a grace period. */
	warn("Don't call closefgrp()");

	if (!f)
		return;
	kref_put(&f->ref);
}

static void freefgrp(struct kref *k)
{
	struct fgrp *f = container_of(k, struct fgrp, ref);
	struct chan *c;
	for (int i = 0; i <= f->maxfd; i++)
		if ((c = f->fd[i]))
			cclose(c);

	kfree(f->fd);
	kfree(f);
}

struct fgrp *newfgrp(void)
{
	struct fgrp *new;
	int n;

	new = kzmalloc(sizeof(struct fgrp), 0);
	kref_init(&new->ref, freefgrp, 1);
	spinlock_init(&new->lock);
	n = DELTAFD;
	new->nfd = n;
	new->fd = kzmalloc(n * sizeof(struct chan *), 0);
	return new;
}

struct fgrp *dupfgrp(struct proc *new_proc, struct fgrp *f)
{
	int i;
	struct chan *c;
	struct fgrp *new;
	int n;

	new = kzmalloc(sizeof(struct fgrp), KMALLOC_WAIT);
	kref_init(&new->ref, freefgrp, 1);
	spin_lock(&f->lock);
	if (f->closed) {
		spin_unlock(&f->lock);
		kfree(new);
		error("File group closed");
	}
	n = DELTAFD;
	if (f->maxfd >= n)
		n = (f->maxfd + 1 + DELTAFD - 1) / DELTAFD * DELTAFD;
	new->nfd = n;
	new->fd = kzmalloc(n * sizeof(struct chan *), 0);
	if (new->fd == NULL) {
		spin_unlock(&f->lock);
		kfree(new);
		error(Enomem);
	}
	new->maxfd = f->maxfd;
	new->minfd = f->minfd;
	for (i = 0; i <= f->maxfd; i++) {
		if ((c = f->fd[i])) {
			kref_get(&c->ref, 1);
			claim_fd(&new_proc->open_files, i);
			new->fd[i] = c;
		}
	}
	spin_unlock(&f->lock);

	return new;
}

struct mount *newmount(struct mhead *mh, struct chan *to, int flag, char *spec)
{
	struct mount *m;

	m = kzmalloc(sizeof(struct mount), 0);
	m->to = to;
	m->head = mh;
	kref_get(&to->ref, 1);
	m->mountid = NEXT_ID(mountid);
	m->mflag = flag;
	if (spec != 0)
		kstrdup(&m->spec, spec);

	return m;
}

void mountfree(struct mount *m)
{
	struct mount *f;

	while (m) {
		f = m->next;
		cclose(m->to);
		m->mountid = 0;
		kfree(m->spec);
		kfree(m);
		m = f;
	}
}

#if 0
almost certainly not needed.void resrcwait(char *reason)
{
	char *p;

	if (current == 0)
		panic("resrcwait");

	p = up->psstate;
	if (reason) {
		up->psstate = reason;
		printd("%s\n", reason);
	}

	udelay_sched(300 * 1000);
	up->psstate = p;
}
#endif

/* TODO: We don't have any alloc / initializer methods for skeyset or signerkey
 * yet.  When we do, use these releases for their kref_init. */
static void __sigs_release(struct kref *kref)
{
	struct skeyset *s = container_of(kref, struct skeyset, ref);
	int i;
	for (i = 0; i < s->nkey; i++)
		freeskey(s->keys[i]);
	kfree(s);
}

void closesigs(struct skeyset *s)
{
	if (!s)
		return;
	kref_put(&s->ref);
}

static void __key_release(struct kref *kref)
{
	struct signerkey *key = container_of(kref, struct signerkey, ref);
	kfree(key->owner);
	(*key->pkfree) (key->pk);
	kfree(key);
}

void freeskey(struct signerkey *key)
{
	if (!key)
		return;
	kref_put(&key->ref);
}
