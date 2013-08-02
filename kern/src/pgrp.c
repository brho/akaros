/*
 * Copyright 2013 Google Inc.
 * Copyright (c) 1989-2003 by Lucent Technologies, Bell Laboratories.
 */
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


enum {
	Whinesecs = 10,		/* frequency of out-of-resources printing */
};

static struct kref pgrpid[1];
static spinlock_t mountidlock[1];
static struct kref mountid[1];

#if 0
/* exception handling. We need to talk.*/
void
pgrpnote(uint32_t noteid, char *a, long n, int flag, struct errbuf *perrbuf)
{
	Proc *p, *ep;
	char buf[ERRMAX];

	if(n >= ERRMAX-1)
		error(Etoobig, perrbuf);

	memmove(buf, a, n);
	buf[n] = 0;
	p = proctab(0, perrbuf);
	ep = p+conf.nproc;
	for(; p < ep; p++) {
		if(p->state == Dead)
			continue;
		if(up != p && p->noteid == noteid && p->kp == 0) {
			qlock(&p->debug, perrbuf);
			if(p->pid == 0 || p->noteid != noteid){
				qunlock(&p->debug, perrbuf);
				continue;
			}
			if(!waserror()) {
				postnote(p, 0, buf, flag, perrbuf);
			}
			qunlock(&p->debug, perrbuf);
		}
	}
}
#endif

struct pgrp*
newpgrp(void)
{
	struct pgrp *p;

	p = kzmalloc(sizeof(struct pgrp), KMALLOC_WAIT);
	kref_init(&p->ref, fake_release, 1);
	kref_get(pgrpid, 1);
	p->pgrpid = kref_refcnt(pgrpid);
	return p;
}

void
closepgrp(struct proc *up, struct pgrp *p, struct errbuf *perrbuf)
{
	struct mhead **h, **e, *f, *next;

	if(kref_put(&p->ref) != 0)
		return;

	/* the p->debug stuff is not needed -- we think. */
	//qlock(&p->debug);
	wlock(&p->ns);
	p->pgrpid = -1;

	e = &p->mnthash[MNTHASH];
	for(h = p->mnthash; h < e; h++) {
		for(f = *h; f; f = next) {
			wlock(&f->lock);
			cclose(f->from, perrbuf);
			mountfree(f->mount, perrbuf);
			f->mount = NULL;
			next = f->hash;
			wunlock(&f->lock);
			putmhead(f, perrbuf);
		}
	}
	wunlock(&p->ns);
	//qunlock(&p->debug);
	kfree(p);
}

void
pgrpinsert(struct mount **order, struct mount *m, struct errbuf *perrbuf)
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
pgrpcpy(struct pgrp *to, struct pgrp *from, struct errbuf *perrbuf)
{
	int i;
	struct mount *n, *m, **link, *order;
	struct mhead *f, **tom, **l, *mh;

	wlock(&from->ns);
	order = 0;
	tom = to->mnthash;
	for(i = 0; i < MNTHASH; i++) {
		l = tom++;
		for(f = from->mnthash[i]; f; f = f->hash) {
			rlock(&f->lock);
			mh = newmhead(f->from, perrbuf);
			*l = mh;
			l = &mh->hash;
			link = &mh->mount;
			for(m = f->mount; m; m = m->next) {
			    n = newmount(mh, m->to, m->mflag, m->spec, perrbuf);
				m->copy = n;
				pgrpinsert(&order, m, perrbuf);
				*link = n;
				link = &n->next;
			}
			runlock(&f->lock);
		}
	}
	/*
	 * Allocate mount ids in the same sequence as the parent group
	 */
	spin_lock(mountidlock);
	/* this may not get the ids right. */
	for(m = order; m; m = m->order)
	    m->copy->mountid = kref_refcnt(mountid);
	kref_get(mountid, 1);
	spin_unlock(mountidlock);
	wunlock(&from->ns);
}

struct fgrp*
dupfgrp(struct fgrp *f, struct errbuf *perrbuf)
{
	struct fgrp *new;
	struct chan *c;
	int i;

	new = kzmalloc(sizeof(struct fgrp), KMALLOC_WAIT);
	if(f == NULL){
		new->fd = kzmalloc(DELTAFD * sizeof(struct chan *), KMALLOC_WAIT);
		new->nfd = DELTAFD;
		kref_init(&new->ref, fake_release, 1);
		return new;
	}

	spin_lock(&f->lock);
	/* Make new fd list shorter if possible, preserving quantization */
	new->nfd = f->maxfd+1;
	i = new->nfd%DELTAFD;
	if(i != 0)
		new->nfd += DELTAFD - i;
	new->fd = kzmalloc(new->nfd * sizeof(struct chan *), KMALLOC_WAIT);
	if(new->fd == NULL){
		spin_unlock(&f->lock);
		kfree(new);
		error("no memory for fgrp", perrbuf);
	}
	kref_init(&new->ref, fake_release, 1);

	new->maxfd = f->maxfd;
	for(i = 0; i <= f->maxfd; i++) {
	    c = f->fd[i];
	    if(c){
			kref_get(&c->ref, 1);
			new->fd[i] = c;
		}
	}
	spin_unlock(&f->lock);

	return new;
}

void
closefgrp(struct proc *up, struct fgrp *f, struct errbuf *perrbuf)
{
	int i;
	struct chan *c;

	if(f == 0)
		return;

	if(kref_put(&f->ref) != 0)
		return;

	/*
	 * If we get into trouble, forceclosefgrp
	 * will bail us out.
	 */
	up->closingfgrp = f;
	for(i = 0; i <= f->maxfd; i++){
	    c = f->fd[i];
	    if(c){
		f->fd[i] = NULL;
		cclose(c, perrbuf);
	    }
	}
	up->closingfgrp = NULL;
	
	kfree(f->fd);
	kfree(f);
}

/*
 * Called from sleep because up is in the middle
 * of closefgrp and just got a kill ctl message.
 * This usually means that up has wedged because
 * of some kind of deadly embrace with mntclose
 * trying to talk to itself.  To break free, hand the
 * unclosed channels to the close queue.  Once they
 * are finished, the blocked cclose that we've 
 * interrupted will finish by itself.
 */
void
forceclosefgrp(struct proc *up, struct errbuf *perrbuf)
{
	int i;
	struct chan *c;
	struct fgrp *f;

	if(/* check that we're exiting somehow ...up->procctl != Proc_exitme || */up->closingfgrp == NULL){
		printd("bad forceclosefgrp call");
		return;
	}

	f = up->closingfgrp;
	for(i = 0; i <= f->maxfd; i++){
	    c = f->fd[i];
	    if(c){
		f->fd[i] = NULL;
		printd("Not calling ccloseq ... fix me\n");
		//ccloseq(c, perrbuf);
	    }
	}
}


struct mount*
newmount(struct mhead *mh, struct chan *to, int flag, char *spec, struct errbuf *perrbuf)
{
	struct mount *m;

	m = kzmalloc(sizeof(struct mount), KMALLOC_WAIT);
	m->to = to;
	m->head = mh;
	kref_get(&to->ref, 1);
	kref_get(mountid, 1);
	m->mountid = kref_refcnt(mountid);
	m->mflag = flag;
	if(spec != 0)
	    kstrdup(&m->spec, spec, perrbuf);

	return m;
}

void
mountfree(struct mount *m, struct errbuf *perrbuf)
{
	struct mount *f;

	while(m) {
		f = m->next;
		cclose(m->to, perrbuf);
		m->mountid = 0;
		kfree(m->spec);
		kfree(m);
		m = f;
	}
}

/* nah */
#if 0
void
resrcwait(struct proc *up, char *reason, struct errbuf *perrbuf)
{
	uint32_t now;
	char *p;
	static uint32_t lastwhine;

	if(up == 0)
		panic("resrcwait");

	p = up->psstate;
	if(reason) {
		up->psstate = reason;
		now = seconds(perrbuf);
		/* don't tie up the console with complaints */
		if(now - lastwhine > Whinesecs) {
			lastwhine = now;
			printd("%s\n", reason);
		}
	}

	tsleep(&up->sleep, return0, 0, 300);
	up->psstate = p;
}
#endif
