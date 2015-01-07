//#define DEBUG
/* Copyright 2014 Google Inc.
 * Copyright (c) 2013 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * devnix/#t: a device for NIX mode
 *
 * A struct nix is a "visitor" chunk of code.  It has a memory image, and can be
 * told to run an arbitrary address (in that image or otherwise) in kernel mode
 * on various pcores, to which it has exclusive access.
 *
 * TODO:
 *
 * - FOR THE MOMENT, this is only intended to run one NIX at a time.  Too many
 * sharp edges for any other mode.
 *
 * - memory images: we have one now for all nixs.  that'll be a mess.
 *
 * - what do we want to do for refcnting?  decref on chan close?  or remove?
 * how do we manage the struct nix memory? (MGMT)
 * 		- right now, we aren't decreffing at all.  it's easier to work with from
 * 		the shell, but it's definitely a debugging thing.  the proper way to do
 * 		these devices is to release on close (i think).  the use case for the
 * 		NIX is a "turn it on once and reboot if you don't like it", so this is
 * 		fine for now.
 * 		- we're using c->aux, which needs to be an uncounted ref, in my opinion.
 * 		i messed around with this for a long time with devsrv, and all the
 * 		different ways 9ns interacts with a device make it very tricky.
 * 		- once we start freeing, we'll need to manage the memory better.  if we
 * 		have holes in the nixs[], we'll need to handle that in nixgen
 *
 * - how are we going to stop a nix?
 * 		- graceful vs immediate?  with some sort of immediate power-cord style
 * 		halting, the entire nix is garbage once we pull the plug.  a more
 * 		graceful style would require the nix to poll or something - probably
 * 		overkill.
 * 		- could send an immediate kmsg (IPI), but we'd need to do some
 * 		bookkeeping to know we're interrupting a NIX and whatnot
 * 		- if we were sure it's a nix core, we might be able to send an immediate
 * 		message telling the core to just smp_idle.  doing that from hard IRQ
 * 		would break a little, so we'd need to be careful (adjust various
 * 		flags, etc).
 * 		- another option would be to hack the halted context and have it call
 * 		a cleanup function (which ultimately smp_idles)
 * 		- if we had a process running the core, and "running the NIX" was a
 * 		syscall or something, we'd want to abort the syscall.  but since the
 * 		syscall isn't trying to rendez or sleep, we couldn't use the existing
 * 		facilities.  so it's the same problem: know it is a nix, somehow
 * 		kill/cleanup.  then just smp_idle.
 * 		- we'll also need to unreserve a core first, so we don't have any
 * 		concurrent startups.  careful of various races with cores coming and
 * 		going.  we can lock the nix before sending the message, but stale RKMs
 * 		could exist for a while.
 * 		- maybe we use a ktask, named nixID or something, to help detect if a
 * 		nix is running.  might also need to track the number of messages sent
 * 		and completed (track completed via the wrapper)
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
#include <arch/types.h>
#include <arch/emulate.h>
#include <arch/vmdebug.h>
#include <kdebug.h>
#include <bitmap.h>

/* qid path types */
enum {
	Qtopdir = 1,
	Qclone,
	Qstat,
	Qnixdir,
	Qctl,
	Qimage,
};

/* The QID is the TYPE and the index into the nix array.
 * We reserve the right to make it an id later. */
#define INDEX_SHIFT 5
/* nix's have an image.
 * Note that the image can be read even as it is running. */
struct nix {
	struct kref kref;
	/* should this be an array of pages? Hmm. */
	void *image;
	unsigned long imagesize;
	int id;
	/* we could dynamically alloc one of these with num_cpus */
	DECLARE_BITMAP(cpus, MAX_NUM_CPUS);
};

static spinlock_t nixlock = SPINLOCK_INITIALIZER_IRQSAVE;
/* array, not linked list. We expect few, might as well be cache friendly. */
static struct nix *nixs = NULL;
static int nnix = 0;
static int nixok = 0;
static int npages;
// only 4 gig for now.
// XXX is this for all nixs?
static page_t *nixpages[1048576];

static atomic_t nixid = 0;

/* The index is not the id, for now.  The index is the spot in nixs[].  The id
 * is an increasing integer, regardless of struct nix* reuse. */
static inline struct nix *QID2NIX(struct qid q)
{
	return &nixs[q.path >> INDEX_SHIFT];
}

static inline int TYPE(struct qid q)
{
	return ((q).path & ((1 << INDEX_SHIFT) - 1));
}

static inline int QID(int index, int type)
{
	return ((index << INDEX_SHIFT) | type);
}

/* TODO: (MGMT) not called yet.  -- we have to unlink the nix */
static void nix_release(struct kref *kref)
{
	struct nix *v = container_of(kref, struct nix, kref);
	spin_lock_irqsave(&nixlock);
	/* cute trick. Save the last element of the array in place of the
	 * one we're deleting. Reduce nnix. Don't realloc; that way, next
	 * time we add a nix the allocator will just return.
	 * Well, this is stupid, because when we do this, we break
	 * the QIDs, which have pointers embedded in them.
	 * darn it, may have to use a linked list. Nope, will probably
	 * just walk the array until we find a matching id. Still ... yuck.
	 *
	 * If we have lots, we can track the lowest free, similar to FDs and low_fd.
	 * honestly, we need an integer allocator (vmem and magazine paper) */
	if (v != &nixs[nnix - 1]) {
		/* free the image ... oops */
		/* get rid of the kref. */
		*v = nixs[nnix - 1];
	}
	nnix--;
	spin_unlock(&nixlock);
}

/* NIX ids run in the range 0..infinity.  */
static int newnixid(void)
{
	return atomic_fetch_and_add(&nixid, 1);
}

static int nixgen(struct chan *c, char *entry_name,
		   struct dirtab *unused, int unused_nr_dirtab,
		   int s, struct dir *dp)
{
	struct qid q;
	struct nix *nix_i;
	/* Whether we're in one dir or at the top, .. still takes us to the top. */
	if (s == DEVDOTDOT) {
		mkqid(&q, Qtopdir, 0, QTDIR);
		devdir(c, c->qid, "#V", 0, eve, 0555, dp);
		return 1;
	}
	switch (TYPE(c->qid)) {
	case Qtopdir:
		/* Generate elements for the top level dir.  We support clone, stat,
		 * nix dirs at the top level */
		if (s == 0) {
			mkqid(&q, Qclone, 0, QTFILE);
			devdir(c, q, "clone", 0, eve, 0666, dp);
			return 1;
		}
		s--;
		if (s == 0) {
			mkqid(&q, Qstat, 0, QTFILE);
			devdir(c, q, "stat", 0, eve, 0666, dp);
			return 1;
		}
		s--;	/* 1 -> 0th element, 2 -> 1st element, etc */
		spin_lock_irqsave(&nixlock);
		if (s >= nnix) {
			spin_unlock(&nixlock);
			return -1;
		}
		nix_i = &nixs[s];
		/* TODO (MGMT): if no nix_i, advance (in case of holes) */
		snprintf(get_cur_genbuf(), GENBUF_SZ, "nix%d", nix_i->id);
		spin_unlock(&nixlock);
		mkqid(&q, QID(s, Qnixdir), 0, QTDIR);
		devdir(c, q, get_cur_genbuf(), 0, eve, 0555, dp);
		return 1;
	case Qnixdir:
		/* Gen the contents of the nix dirs */
		s += Qctl;	/* first time through, start on Qctl */
		switch (s) {
		case Qctl:
			mkqid(&q, QID(s-Qctl, Qctl), 0, QTFILE);
			devdir(c, q, "ctl", 0, eve, 0666, dp);
			return 1;
		case Qimage:
			mkqid(&q, QID(s-Qctl, Qimage), 0, QTFILE);
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
	case Qstat:
		devdir(c, c->qid, "stat", 0, eve, 0444, dp);
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

void nixtest(void)
{
	printk("nixtest ran on core %d\n", core_id());
}

// allocate pages, starting at 1G, and running until we run out.
static void nixinit(void)
{
	//error_t kpage_alloc_specific(page_t** page, size_t ppn)
	uint64_t ppn = GiB/4096;
	while (1) {
		if (!page_is_free(ppn)) {
			printk("%s: got a non-free page@ppn %p\n", __func__, ppn);
			break;
		}
		kpage_alloc_specific(&nixpages[ppn], ppn);
		npages++;
		ppn++;
	}
	printk("nixinit: nix_init returns %d\n", npages);

	if (npages > 0) {
		nixok = 1;
	}
}

static struct chan *nixattach(char *spec)
{
	if (!nixok)
		error("No NIXs available");
	struct chan *c = devattach('t', spec);
	mkqid(&c->qid, Qtopdir, 0, QTDIR);
	return c;
}

static struct walkqid *nixwalk(struct chan *c, struct chan *nc, char **name,
				int nname)
{
	return devwalk(c, nc, name, nname, 0, 0, nixgen);
}

static int nixstat(struct chan *c, uint8_t * db, int n)
{
	return devstat(c, db, n, 0, 0, nixgen);
}

/* It shouldn't matter if p = current is DYING.  We'll eventually fail to insert
 * the open chan into p's fd table, then decref the chan. */
static struct chan *nixopen(struct chan *c, int omode)
{
	ERRSTACK(1);
	struct nix *v = QID2NIX(c->qid);
	if (waserror()) {
		nexterror();
	}
	switch (TYPE(c->qid)) {
	case Qtopdir:
	case Qnixdir:
		if (omode & ORCLOSE)
			error(Eperm);
		if (!IS_RDONLY(omode))
			error(Eisdir);
		break;
	case Qclone:
		spin_lock_irqsave(&nixlock);
		if (nnix >= 1) {
			spin_unlock_irqsave(&nixlock);
			set_errno(EBUSY);
			error("Already have 1 nix, we don't support more");
		}
		nixs = krealloc(nixs, sizeof(nixs[0]) * (nnix + 1), 0);
		v = &nixs[nnix];
		mkqid(&c->qid, QID(nnix, Qctl), 0, QTFILE);
		nnix++;
		spin_unlock(&nixlock);
		kref_init(&v->kref, nix_release, 1);
		v->id = newnixid();
		v->image = KADDR(GiB);
		v->imagesize = npages * 4096;
		printk("nix image is %p with %d bytes\n", v->image, v->imagesize);
		c->aux = v;
		bitmap_zero(v->cpus, MAX_NUM_CPUS);
		break;
	case Qstat:
		break;
	case Qctl:
	case Qimage:
		/* TODO: (MGMT) refcnting */
		//kref_get(&v->kref, 1);
		c->aux = QID2NIX(c->qid);
		break;
	}
	c->mode = openmode(omode);
	/* Assumes c is unique (can't be closed concurrently */
	c->flag |= COPEN;
	c->offset = 0;
	poperror();
	return c;
}

static void nixcreate(struct chan *c, char *name, int omode, uint32_t perm)
{
	error(Eperm);
}

static void nixremove(struct chan *c)
{
	error(Eperm);
}

static int nixwstat(struct chan *c, uint8_t * dp, int n)
{
	error("No nixwstat");
	return 0;
}

static void nixclose(struct chan *c)
{
	struct nix *v = c->aux;
	if (!v)
		return;
	/* There are more closes than opens.  For instance, sysstat doesn't open,
	 * but it will close the chan it got from namec.  We only want to clean
	 * up/decref chans that were actually open. */
	if (!(c->flag & COPEN))
		return;
	switch (TYPE(c->qid)) {
		/* TODO: (MGMT) the idea of 'stopping' a nix is tricky.
		 * for now, leave the NIX active even when we close ctl */
	case Qctl:
		break;
	case Qimage:
		//kref_put(&v->kref);
		break;
	}
}

static long nixread(struct chan *c, void *ubuf, long n, int64_t offset)
{
	struct nix *v = c->aux;
	switch (TYPE(c->qid)) {
	case Qtopdir:
	case Qnixdir:
		return devdirread(c, ubuf, n, 0, 0, nixgen);
	case Qstat:
		return readnum(offset, ubuf, n, nnix, NUMSIZE32);
	case Qctl:
		assert(v);
		return readnum(offset, ubuf, n, v->id, NUMSIZE32);
	case Qimage:
		assert(v);
		return readmem(offset, ubuf, n, v->image, v->imagesize);
	default:
		panic("Bad QID %p in devnix", c->qid.path);
	}
	return 0;
}

static void nixwrapper(uint32_t srcid, long a0, long a1, long a2)
{
	void (*f)(void) = (void (*)(void))a0;
	f();
	/* TODO: could do some tracking to say this message has been completed */
}

static long nixwrite(struct chan *c, void *ubuf, long n, int64_t off)
{
	struct nix *v = c->aux;
	ERRSTACK(1);
	char buf[32];
	struct cmdbuf *cb;
	struct nix *nix;
	uint64_t hexval;
	switch (TYPE(c->qid)) {
	case Qtopdir:
	case Qnixdir:
	case Qstat:
		error(Eperm);
	case Qctl:
		nix = c->aux;
		cb = parsecmd(ubuf, n);
		/* TODO: lock the nix here, unlock in waserror and before popping */
		if (waserror()) {
			kfree(cb);
			nexterror();
		}
		if (!strcmp(cb->f[0], "run")) {
			int core;
			uintptr_t ip;
			if (cb->nf != 3)
				error("usage: run core entry");
			core = strtoul(cb->f[1], 0, 0);
			ip = strtoul(cb->f[2], 0, 0);
			if (!test_bit(core, nix->cpus))
				error("Bad core %d", core);
			send_kernel_message(core, nixwrapper, (long)ip, 0, 0, KMSG_ROUTINE);
		} else if (!strcmp(cb->f[0], "test")) {
			int core;
			if (cb->nf != 2)
				error("usage: test core");
			core = strtoul(cb->f[1], 0, 0);
			if (!test_bit(core, nix->cpus))
				error("Bad core %d", core);
			send_kernel_message(core, nixwrapper, (long)nixtest, 0, 0,
			                    KMSG_ROUTINE);
		} else if (!strcmp(cb->f[0], "reserve")) {
			int core;
			if (cb->nf != 2)
				error("Usage: reserve core (-1 for any)");
			core = strtol(cb->f[1], 0, 0);
			if (core == -1) {
				core = get_any_idle_core();
				if (core < 0)
					error("No free idle cores!");
			} else {
				if (get_this_idle_core(core) < 0)
					error("Failed to reserve core %d\n", core);
			}
			set_bit(core, nix->cpus);
		} else if (!strcmp(cb->f[0], "check")) {
			int i;
			for(i = 0; i < MAX_NUM_CPUS; i++) {
				if (!test_bit(i, nix->cpus))
					continue;
				printk("Core %d is available to nix%d\n", i, nix->id);
			}
		} else if (!strcmp(cb->f[0], "stop")) {
			error("can't stop a nix yet");
		} else {
			error("%s: not implemented", cb->f[0]);
		}
		kfree(cb);
		poperror();
		break;
	case Qimage:
		if (off < 0)
			error("offset < 0!");

		if (off + n > v->imagesize) {
			n = v->imagesize - off;
		}

		if (memcpy_from_user_errno(current, v->image + off, ubuf, n) < 0)
			error("%s: bad user addr %p", __FUNCTION__, ubuf);
		break;

	default:
		panic("Bad QID %p in devnix", c->qid.path);
	}
	return n;
}

struct dev nixdevtab __devtab = {
	't',
	"nix",

	devreset,
	nixinit,
	devshutdown,
	nixattach,
	nixwalk,
	nixstat,
	nixopen,
	nixcreate,
	nixclose,
	nixread,
	devbread,
	nixwrite,
	devbwrite,
	nixremove,
	nixwstat,
	devpower,
//  devconfig,
	devchaninfo,
};
