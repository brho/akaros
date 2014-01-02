#define DEBUG
/* Copyright 2014 Google Inc.
 * Copyright (c) 2013 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * devvm/#V: a device for VMs
 *
 */

#include <kmalloc.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <error.h>
#include <pmap.h>
#include <sys/queue.h>
#include <smp.h>
#include <kref.h>
#include <atomic.h>
#include <alarm.h>
#include <event.h>
#include <umem.h>
#include <devalarm.h>

/* qid path types */
enum {
	Qtopdir = 1,
	Qclone,
	Qstat,
	Qvmdir,
	Qctl,
	Qimage,
};

/* This paddr/kaddr is a bit dangerous.  it'll work so long as we don't need all
 * 64 bits for a physical address (48 is the current norm on x86_64).
 * We're probably going to move to a model where we put the VM index or something
 * into the qid, but this works for now.
 */
#define ADDR_SHIFT 5
#define QID2VM(q) ((struct proc_alarm*)KADDR(((q).path >> ADDR_SHIFT)))
#define TYPE(q) ((q).path & ((1 << ADDR_SHIFT) - 1))
#define QID(ptr, type) ((PADDR(ptr) << ADDR_SHIFT) | type)

/* vm's have an image.
 * Note that the image can be read even as it is running. */
struct vm {
	struct vm *next;
	struct kref					kref;
	/* should this be an array of pages? Hmm. */
	void                                           *image;
	unsigned long                                   imagesize;
	int                                             id;
};

static spinlock_t vmlock;
/* array, not linked list. We expect few, might as well be cache friendly. */
static struct vm *vms = NULL;
static int nvm = 0;

static spinlock_t vmidlock[1];
static struct kref vmid[1] = { {(void *)1, fake_release} };

static void vm_release(struct kref *kref)
{
	struct vm *v = container_of(kref, struct vm, kref);
	spin_lock(&vmlock);
	/* cute trick. Save the last element of the array in place of the
	 * one we're deleting. Reduce nvm. Don't realloc; that way, next

	 * do and just return.
	 */
	if (v != &vms[nvm-1]){
		/* free the image ... oops */
		/* get rid of the kref. */
		*v = vms[nvm-1];
	}
	nvm--;
	spin_unlock(&vmlock);
}

static int newvmid(void)
{
	int id;
	spin_lock(vmidlock);
	id = kref_refcnt(vmid);
	kref_get(vmid, 1);
	spin_unlock(vmidlock);
	return id;
}

static int vmgen(struct chan *c, char *entry_name,
		 struct dirtab *unused, int unused_nr_dirtab,
		 int s, struct dir *dp)
{
	struct qid q;
	struct vm *vm_i;
	DEBUG("GEN s %d\n", s);
	/* Whether we're in one dir or at the top, .. still takes us to the top. */
	if (s == DEVDOTDOT) {
		mkqid(&q, Qtopdir, 0, QTDIR);
		devdir(c, c->qid, "#V", 0, eve, 0555, dp);
		return 1;
	}
	DEBUG("TYPE %d\n", TYPE(c->qid));
	switch (TYPE(c->qid)) {
	case Qtopdir:
		DEBUG("Qtopdir s %d nvm %d\n", s, nvm);
		/* Generate elements for the top level dir.  We support a clone and
		 * vm dirs at the top level */
		if (s == 0) {
			mkqid(&q, Qclone, 0, QTFILE);
			devdir(c, q, "clone", 0, eve, 0666, dp);
			return 1;
		}
		s--;	/* 1 -> 0th element, 2 -> 1st element, etc */
		spin_lock(&vmlock);
		if (s >= nvm){
			DEBUG("DONE qtopdir\n");
			spin_unlock(&vmlock);
			return -1;
		}
		vm_i = &vms[s];
		snprintf(get_cur_genbuf(), GENBUF_SZ, "vm%d", vm_i->id);
		spin_unlock(&vmlock);
		mkqid(&q, QID(vm_i, Qvmdir), 0, QTDIR);
		devdir(c, q, get_cur_genbuf(), 0, eve, 0555, dp);
		return 1;
	case Qvmdir:
		/* Gen the contents of the vm dirs */
		s += Qctl;	/* first time through, start on Qctl */
		switch (s) {
		case Qctl:
			mkqid(&q, QID(QID2VM(c->qid), Qctl), 0, QTFILE);
			devdir(c, q, "ctl", 0, eve, 0666, dp);
			return 1;
		case Qimage:
			mkqid(&q, QID(QID2VM(c->qid), Qimage), 0, QTFILE);
			devdir(c, q, "image", 0, eve, 0666, dp);
			return 1;
		}
		return -1;
		/* Need to also provide a direct hit for Qclone and all other files (at
		 * all levels of the hierarchy).  Every file is both
		 * generated (via the s increments in their respective directories) and
		 * directly gen-able.  devstat() will call gen with a specific path in
		 * the qid.  In these cases, we make a dir for whatever they are asking
		 * for.  Note the qid stays the same.  I think this is what the old
		 * plan9 comments above devgen were talking about for (ii).
		 *
		 * We don't need to do this for the directories - devstat will look for
		 * the a directory by path and fail.  Then it will manually build the
		 * stat output (check the -1 case in devstat). */
	case Qclone:
		devdir(c, c->qid, "clone", 0, eve, 0666, dp);
		return 1;
	case Qctl:
		devdir(c, c->qid, "ctl", 0, eve, 0666, dp);
		return 1;
	case Qimage:
		devdir(c, c->qid, "image", 0, eve, 0666, dp);
		return 1;
	}
	return -1;
}

static void vminit(void)
{
	spinlock_init(&vmlock);
	spinlock_init(vmidlock);
}

static struct chan *vmattach(char *spec)
{
	struct chan *c = devattach('V', spec);
	mkqid(&c->qid, Qtopdir, 0, QTDIR);
	return c;
}

static struct walkqid *vmwalk(struct chan *c, struct chan *nc, char **name,
			      int nname)
{
	return devwalk(c, nc, name, nname, 0, 0, vmgen);
}

static long vmstat(struct chan *c, uint8_t *db, long n)
{
	return devstat(c, db, n, 0, 0, vmgen);
}

/* It shouldn't matter if p = current is DYING.  We'll eventually fail to insert
 * the open chan into p's fd table, then decref the chan. */
static struct chan *vmopen(struct chan *c, int omode)
{
	struct vm *v = c->aux;
	switch (TYPE(c->qid)) {
	case Qtopdir:
	case Qvmdir:
		if (omode & ORCLOSE)
			error(Eperm);
		if (omode != OREAD)
			error(Eisdir);
		break;
	case Qclone:
		spin_lock(&vmlock);
		vms = krealloc(vms, sizeof(vms[0])*(nvm+1),0);
		v = &vms[nvm];
		nvm++;
		spin_unlock(&vmlock);
		kref_init(&v->kref, vm_release, 1);
		v->id = newvmid();
		mkqid(&c->qid, QID(v, Qctl), 0, QTFILE);
		c->aux = v;
		break;
	case Qctl:
	case Qimage:
		/* the purpose of opening is to hold a kref on the proc_vm */
		v = c->aux;
		/* this isn't a valid pointer yet, since our chan doesn't have a
		 * ref.  since the time that walk gave our chan the qid, the chan
		 * could have been closed, and the vm decref'd and freed.  the
		 * qid is essentially an uncounted reference, and we need to go to
		 * the source to attempt to get a real ref.  Unfortunately, this is
		 * another scan of the list, same as devsrv.  We could speed it up
		 * by storing an "on_list" bool in the vm_is. */
		//if (!vm_i)
		//	error("Unable to open vm, concurrent closing");
		break;
	}
	c->mode = openmode(omode);
	/* Assumes c is unique (can't be closed concurrently */
	c->flag |= COPEN;
	c->offset = 0;
	return c;
}

static void vmcreate(struct chan *c, char *name, int omode, int perm)
{
	error(Eperm);
}

static void vmremove(struct chan *c)
{
	error(Eperm);
}

static long vmwstat(struct chan *c, uint8_t *dp, long n)
{
	error("No vmwstat");
	return 0;
}

static void vmclose(struct chan *c)
{
	struct vm *v = c->aux;
	if (!v)
		return;
	/* There are more closes than opens.  For instance, sysstat doesn't open,
	 * but it will close the chan it got from namec.  We only want to clean
	 * up/decref chans that were actually open. */
	if (!(c->flag & COPEN))
		return;
	switch (TYPE(c->qid)) {
	case Qctl:
	case Qimage:
		kref_put(&v->kref);
		break;
	}
}

static long vmread(struct chan *c, void *ubuf, long n, int64_t offset)
{
	struct vm *v = c->aux;
	switch (TYPE(c->qid)) {
	case Qtopdir:
	case Qvmdir:
		return devdirread(c, ubuf, n, 0, 0, vmgen);
	case Qctl:
		return readnum(offset, ubuf, n, v->id, NUMSIZE32);
 	case Qimage:
		return readmem(offset, ubuf, n,
			       v->image, v->imagesize);
	default:
		panic("Bad QID %p in devvm", c->qid.path);
	}
	return 0;
}

static long vmwrite(struct chan *c, void *ubuf, long n, int64_t unused)
{
	ERRSTACK(1);
	char buf[32];
	struct cmdbuf *cb;
	struct vm *vm;
	uint64_t hexval;

	switch (TYPE(c->qid)) {
	case Qtopdir:
	case Qvmdir:
		error(Eperm);
	case Qctl:
		vm = c->aux;
		cb = parsecmd(ubuf, n);
		if (waserror()) {
			kfree(cb);
			nexterror();
		}
		if (!strcmp(cb->f[0], "start")) {
			error("can't run a vm yet");
		} else if (!strcmp(cb->f[0], "stop")) {
			error("can't stop a vm yet");
		} else {
			error("%s: not implemented", cb->f[0]);
		}
		kfree(cb);
		poperror();
		break;
	case Qimage:
		error("can't write an image yet");
		break;
	default:
		panic("Bad QID %p in devvm", c->qid.path);
	}
	return n;
}

struct dev vmdevtab = {
	'V',
	"vm",

	devreset,
	vminit,
	devshutdown,
	vmattach,
	vmwalk,
	vmstat,
	vmopen,
	vmcreate,
	vmclose,
	vmread,
	devbread,
	vmwrite,
	devbwrite,
	vmremove,
	vmwstat,
	devpower,
	devconfig,
	devchaninfo,
};
