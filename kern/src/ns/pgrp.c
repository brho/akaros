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

static struct kref pgrpid;
static struct kref mountid;

void
closepgrp(struct pgrp *p)
{
	struct mhead **h, **e, *f, *next;
	
	wlock(&p->ns);
	p->pgrpid = -1;

	e = &p->mnthash[MNTHASH];
	for(h = p->mnthash; h < e; h++) {
		for(f = *h; f; f = next) {
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

static void
freepgrp(struct kref *k)
{
	struct pgrp *p = container_of(k, struct pgrp, ref);
	closepgrp(p);
}

struct pgrp*
newpgrp(void)
{
	struct pgrp *p;

	p = kzmalloc(sizeof(struct pgrp), 0);
	kref_init(&p->ref, freepgrp, 1);
	kref_get(&pgrpid, 1);
	p->pgrpid = kref_refcnt(&pgrpid);
	p->progmode = 0644;
	return p;
}

void
pgrpinsert(struct mount **order, struct mount *m)
{
	struct mount *f;

	m->order = 0;
	if(*order == 0) {
		*order = m;
		return;
	}
	for(f = *order; f; f = f->order) {
		if(m->mountid < f->mountid) {
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
void
pgrpcpy(struct pgrp *to, struct pgrp *from)
{
	ERRSTACK(2);
	int i;
	struct mount *n, *m, **link, *order;
	struct mhead *f, **tom, **l, *mh;

	wlock(&from->ns);
	if(waserror()){
		wunlock(&from->ns);
		nexterror();
	}
	order = 0;
	tom = to->mnthash;
	for(i = 0; i < MNTHASH; i++) {
		l = tom++;
		for(f = from->mnthash[i]; f; f = f->hash) {
			rlock(&f->lock);
			if(waserror()){
				runlock(&f->lock);
				nexterror();
			}
			mh = kzmalloc(sizeof(struct mhead), 0);
			if(mh == NULL)
				error(Enomem);
			mh->from = f->from;
			kref_init(&mh->ref, fake_release, 1);
			kref_get(&mh->from->ref, 1);
			*l = mh;
			l = &mh->hash;
			link = &mh->mount;
			for(m = f->mount; m; m = m->next) {
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
	for(m = order; m; m = m->order){
		kref_get(&mountid, 1);
		m->copy->mountid = kref_refcnt(&mountid);
	}

	to->progmode = from->progmode;
	to->slash = cclone(from->slash);
	to->dot = cclone(from->dot);
	to->nodevs = from->nodevs;

	poperror();
	wunlock(&from->ns);
}

void
closefgrp(struct fgrp *f)
{
	int i;
	struct chan *c;

	if(f == NULL || kref_put(&f->ref) != 0)
		return;

	for(i = 0; i <= f->maxfd; i++)
		if((c = f->fd[i]))
			cclose(c);

	kfree(f->fd);
	kfree(f);
}

static void
freefgrp(struct kref *k)
{
	struct fgrp *f = container_of(k, struct fgrp, ref);
	closefgrp(f);
}

struct fgrp*
newfgrp(struct fgrp *old)
{
	struct fgrp *new;
	int n;

	new = kzmalloc(sizeof(struct fgrp), 0);
	kref_init(&new->ref, freefgrp, 1);
	n = DELTAFD;
	if(old != NULL){
		spin_lock(&old->lock);
		if(old->maxfd >= n)
			n = (old->maxfd+1 + DELTAFD-1)/DELTAFD * DELTAFD;
		new->maxfd = old->maxfd;
		spin_unlock(&old->lock);
	}
	new->nfd = n;
	new->fd = kzmalloc(n * sizeof(struct chan *), 0);
	return new;
}

struct fgrp*
dupfgrp(struct fgrp *f)
{
	int i;
	struct chan *c;
	struct fgrp *new;
	int n;

	new = kzmalloc(sizeof(struct fgrp), 0);
	kref_init(&new->ref, freefgrp, 1);
	spin_lock(&f->lock);
	n = DELTAFD;
	if(f->maxfd >= n)
		n = (f->maxfd+1 + DELTAFD-1)/DELTAFD * DELTAFD;
	new->nfd = n;
	new->fd = kzmalloc(n * sizeof(struct chan *), 0);
	if(new->fd == NULL){
		spin_unlock(&f->lock);
		kfree(new);
		error(Enomem);
	}
	new->maxfd = f->maxfd;
	new->minfd = f->minfd;
	for(i = 0; i <= f->maxfd; i++) {
		if((c = f->fd[i])){
			kref_get(&c->ref, 1);
			new->fd[i] = c;
		}
	}
	spin_unlock(&f->lock);

	return new;
}

struct mount*
newmount(struct mhead *mh, struct chan *to, int flag, char *spec)
{
	struct mount *m;

	m = kzmalloc(sizeof(struct mount), 0);
	m->to = to;
	m->head = mh;
	kref_get(&to->ref, 1);
	kref_get(&mountid, 1);
	m->mountid = kref_refcnt(&mountid);
	m->mflag = flag;
	if(spec != 0)
		kstrdup(&m->spec, spec);

	return m;
}

void
mountfree(struct mount *m)
{
	struct mount *f;

	while(m) {
		f = m->next;
		cclose(m->to);
		m->mountid = 0;
		kfree(m->spec);
		kfree(m);
		m = f;
	}
}

#if 0
almost certainly not needed.
void
resrcwait(char *reason)
{
	char *p;

	if(current == 0)
		panic("resrcwait");

	p = up->psstate;
	if(reason) {
		up->psstate = reason;
		printd("%s\n", reason);
	}

	udelay_sched(300 * 1000);
	up->psstate = p;
}
#endif

void
closesigs(struct skeyset *s)
{
	int i;

	if(s == NULL || kref_put(&s->ref) != 0)
		return;
	for(i=0; i<s->nkey; i++)
		freeskey(s->keys[i]);
	kfree(s);
}

void
freeskey(struct signerkey *key)
{
	if(key == NULL || kref_put(&key->ref) != 0)
		return;
	kfree(key->owner);
	(*key->pkfree)(key->pk);
	kfree(key);
}
