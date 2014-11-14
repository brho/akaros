//#define DEBUG
/* Copyright 2014 Google Inc.
 * Copyright (c) 2013 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * devnix/#t: a device for NIX mode
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
#include <arch/types.h>
#include <arch/nix.h>
#include <arch/emulate.h>
#include <arch/vmdebug.h>
#include <kdebug.h>

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
 * We reserve the right to make it an id later.
 */
#define ID_SHIFT 5
/* nix's have an image.
 * Note that the image can be read even as it is running. */
struct nix {
	struct kref kref;
	/* should this be an array of pages? Hmm. */
	void *image;
	unsigned long imagesize;
	int id; // not used yet.
};

// NIX style application cores for now.
// we set result to zero. We spin on assignment.
// assigner sets work pointer and then assignment IP.
// once assigned, we run until done, then set result non-zero.
struct mycore {
	void (*assignment)(void);
	void *work;
	void *result;
	void *done;
	int usable;
	int state;
};

static struct mycore allcores[32];
static spinlock_t nixlock;
/* array, not linked list. We expect few, might as well be cache friendly. */
static struct nix *nixs = NULL;
static int nnix = 0;
static int nixok = 0;
static int npages;
// only 4 gig for now.
static page_t *nixpages[1048576];

static spinlock_t nixidlock[1];
static struct kref nixid[1] = { {(void *)1, fake_release} };

//int nix_run(struct nix *nix, struct nix_run *nix_run);

static inline struct nix *
QID2NIX(struct qid q)
{
	return &nixs[((q).vers)];
}

static inline int
TYPE(struct qid q)
{
	return ((q).path & ((1 << ID_SHIFT) - 1));
}

static inline int QID(int index, int type)
{
	return ((index << ID_SHIFT) | type);
}

/* we'll need this somewhere more generic. */
static void readn(struct chan *c, void *vp, long n)
{
	print_func_entry();
	char *p;
	long nn;
	int total = 0, want = n;

	p = vp;
	while (n > 0) {
		nn = devtab[c->type].read(c, p, n, c->offset);
		printk("readn: Got %d@%lld\n", nn, c->offset);
		if (nn == 0)
			error("%s: wanted %d, got %d", Eshort, want, total);
		c->offset += nn;
		p += nn;
		n -= nn;
		total += nn;
	}
	print_func_exit();
}

/* not called yet.  -- we have to unlink the nix */
static void nix_release(struct kref *kref)
{
	print_func_entry();
	struct nix *v = container_of(kref, struct nix, kref);
	spin_lock_irqsave(&nixlock);
	/* cute trick. Save the last element of the array in place of the
	 * one we're deleting. Reduce nnix. Don't realloc; that way, next
	 * time we add a nix the allocator will just return.
	 * Well, this is stupid, because when we do this, we break
	 * the QIDs, which have pointers embedded in them.
	 * darn it, may have to use a linked list. Nope, will probably
	 * just walk the array until we find a matching id. Still ... yuck.
	 */
	if (v != &nixs[nnix - 1]) {
		/* free the image ... oops */
		/* get rid of the kref. */
		*v = nixs[nnix - 1];
	}
	nnix--;
	spin_unlock(&nixlock);
	print_func_exit();
}

/* NIX ids run in the range 1..infinity.
 */
static int newnixid(void)
{
	print_func_entry();
	int id;
	spin_lock_irqsave(nixidlock);
	id = kref_refcnt(nixid);
	kref_get(nixid, 1);
	spin_unlock(nixidlock);
	print_func_exit();
	return id - 1;
}

static int nixgen(struct chan *c, char *entry_name,
		   struct dirtab *unused, int unused_nr_dirtab,
		   int s, struct dir *dp)
{
	print_func_entry();
	struct qid q;
	struct nix *nix_i;
	printd("GEN s %d\n", s);
	/* Whether we're in one dir or at the top, .. still takes us to the top. */
	if (s == DEVDOTDOT) {
		mkqid(&q, Qtopdir, 0, QTDIR);
		devdir(c, c->qid, "#V", 0, eve, 0555, dp);
		print_func_exit();
		return 1;
	}
	printd("TYPE %d\n", TYPE(c->qid));
	switch (TYPE(c->qid)) {
	case Qtopdir:
		printd("Qtopdir s %d nnix %d\n", s, nnix);
		/* Generate elements for the top level dir.  We support clone, stat,
		 * nix dirs at the top level */
		if (s == 0) {
			mkqid(&q, Qclone, 0, QTFILE);
			devdir(c, q, "clone", 0, eve, 0666, dp);
			print_func_exit();
			return 1;
		}
		s--;
		if (s == 0) {
			mkqid(&q, Qstat, 0, QTFILE);
			devdir(c, q, "stat", 0, eve, 0666, dp);
			print_func_exit();
			return 1;
		}
		s--;	/* 1 -> 0th element, 2 -> 1st element, etc */
		spin_lock_irqsave(&nixlock);
		if (s >= nnix) {
			printd("DONE qtopdir\n");
			spin_unlock(&nixlock);
			print_func_exit();
			return -1;
		}
		nix_i = &nixs[s];
		snprintf(get_cur_genbuf(), GENBUF_SZ, "nix%d", nix_i->id);
		spin_unlock(&nixlock);
		mkqid(&q, QID(s, Qnixdir), 0, QTDIR);
		devdir(c, q, get_cur_genbuf(), 0, eve, 0555, dp);
		print_func_exit();
		return 1;
	case Qnixdir:
		/* Gen the contents of the nix dirs */
		s += Qctl;	/* first time through, start on Qctl */
		switch (s) {
		case Qctl:
			mkqid(&q, QID(s-Qctl, Qctl), 0, QTFILE);
			devdir(c, q, "ctl", 0, eve, 0666, dp);
			print_func_exit();
			return 1;
		case Qimage:
			mkqid(&q, QID(s-Qctl, Qimage), 0, QTFILE);
			devdir(c, q, "image", 0, eve, 0666, dp);
			print_func_exit();
			return 1;
		}
		print_func_exit();
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
		print_func_exit();
		return 1;
	case Qstat:
		devdir(c, c->qid, "stat", 0, eve, 0444, dp);
		print_func_exit();
		return 1;
	case Qctl:
		devdir(c, c->qid, "ctl", 0, eve, 0666, dp);
		print_func_exit();
		return 1;
	case Qimage:
		devdir(c, c->qid, "image", 0, eve, 0666, dp);
		print_func_exit();
		return 1;
	}
	print_func_exit();
	return -1;
}

void nixtest(void)
{
	int core = hw_core_id();
	allcores[core].state = 2;
	wmb();
	printk("nixtest\n");
	allcores[core].assignment = 0;
	wmb();
}

void nixhost(uint32_t srcid, long a0, long a1, long a2)
{
	int core = a0;
	printk("nixhost server starting: %d %d %d %d\n", srcid, core, a1, a2);
	allcores[core].assignment = 0;
	allcores[core].usable = 1;
	allcores[core].state = 0;
	while (1) {
		while (allcores[core].assignment == (void *)0) {
			allcores[core].state = 0;
			wmb();
			//printk("mwait for assignment\n");
			mwait(&allcores[core].assignment);
		}
		allcores[core].state = 1;
		wmb();
		printk("Core %d assigned %p\n", allcores[core].assignment);
		(allcores[core].assignment)();
		allcores[core].assignment = 0;
		allcores[core].state = 2;
		wmb();
	}
}

// allocate pages, starting at 1G, and running until we run out.
static void nixinit(void)
{
	//error_t kpage_alloc_specific(page_t** page, size_t ppn)
	print_func_entry();
	uint64_t ppn = GiB/4096;
	spinlock_init_irqsave(&nixlock);
	spinlock_init_irqsave(nixidlock);
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

	// are your cpu etc. etc.
	// there has to be a better way but for now make it work.
	int seen_0 = 0;
	struct sched_pcore *p;
	extern struct sched_pcore_tailq idlecores;
	extern struct sched_pcore *all_pcores;
	TAILQ_FOREACH(p, &idlecores, alloc_next) {
		int coreid = p - all_pcores;
		if (! coreid) {
			if (seen_0++ > 1)
				break;
		}
		if (coreid > 3){
			TAILQ_REMOVE(&idlecores, p, alloc_next);
			send_kernel_message(coreid, nixhost, coreid, 0, 0,
					    KMSG_ROUTINE);
			warn("Using core %d for the ARSCs - there are probably issues with this.", coreid);
			break;

		}
	}
	print_func_exit();
}

static struct chan *nixattach(char *spec)
{
	print_func_entry();
	if (!nixok)
		error("No NIXs available");
	struct chan *c = devattach('t', spec);
	mkqid(&c->qid, Qtopdir, 0, QTDIR);
	print_func_exit();
	return c;
}

static struct walkqid *nixwalk(struct chan *c, struct chan *nc, char **name,
				int nname)
{
	print_func_entry();
	print_func_exit();
	return devwalk(c, nc, name, nname, 0, 0, nixgen);
}

static int nixstat(struct chan *c, uint8_t * db, int n)
{
	print_func_entry();
	print_func_exit();
	return devstat(c, db, n, 0, 0, nixgen);
}

/* It shouldn't matter if p = current is DYING.  We'll eventually fail to insert
 * the open chan into p's fd table, then decref the chan. */
static struct chan *nixopen(struct chan *c, int omode)
{
	print_func_entry();
	ERRSTACK(1);
	struct nix *v = QID2NIX(c->qid);
	printk("nixopen: v is %p\n", v);
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
		nixs = krealloc(nixs, sizeof(nixs[0]) * (nnix + 1), 0);
		v = &nixs[nnix];
		mkqid(&c->qid, QID(nnix, Qctl), 0, QTFILE);
		nnix++;
		spin_unlock(&nixlock);
		kref_init(&v->kref, nix_release, 1);
		v->id = newnixid();
		v->image = KADDR(GiB);
		v->imagesize = npages * 4096;
		c->aux = v;
		printd("New NIX id %d @ %p\n", v->id, v);
		printk("image is %p with %d bytes\n", v->image, v->imagesize);
		printk("Qclone open: id %d, v is %p\n", nnix-1, v);
		break;
	case Qstat:
		break;
	case Qctl:
	case Qimage:
		//kref_get(&v->kref, 1);
		c->aux = QID2NIX(c->qid);
		printk("open qctl/image: aux (nix) is %p\n", c->aux);
		break;
	}
	c->mode = openmode(omode);
	/* Assumes c is unique (can't be closed concurrently */
	c->flag |= COPEN;
	c->offset = 0;
	poperror();
	print_func_exit();
	return c;
}

static void nixcreate(struct chan *c, char *name, int omode, uint32_t perm)
{
	print_func_entry();
	error(Eperm);
	print_func_exit();
}

static void nixremove(struct chan *c)
{
	print_func_entry();
	error(Eperm);
	print_func_exit();
}

static int nixwstat(struct chan *c, uint8_t * dp, int n)
{
	print_func_entry();
	error("No nixwstat");
	print_func_exit();
	return 0;
}

static void nixclose(struct chan *c)
{
	print_func_entry();
	struct nix *v = c->aux;
	if (!v) {
		print_func_exit();
		return;
	}
	/* There are more closes than opens.  For instance, sysstat doesn't open,
	 * but it will close the chan it got from namec.  We only want to clean
	 * up/decref chans that were actually open. */
	if (!(c->flag & COPEN)) {
		print_func_exit();
		return;
	}
	switch (TYPE(c->qid)) {
		/* for now, leave the NIX active even when we close ctl */
	case Qctl:
		break;
	case Qimage:
		//kref_put(&v->kref);
		break;
	}
	print_func_exit();
}

static long nixread(struct chan *c, void *ubuf, long n, int64_t offset)
{
	print_func_entry();
	struct nix *v = c->aux;
	printd("NIXREAD\n");
	switch (TYPE(c->qid)) {
	case Qtopdir:
	case Qnixdir:
		print_func_exit();
		return devdirread(c, ubuf, n, 0, 0, nixgen);
	case Qstat:
		print_func_exit();
		return readnum(offset, ubuf, n, nnix, NUMSIZE32);
	case Qctl:
		assert(v);
		print_func_exit();
		return readnum(offset, ubuf, n, v->id, NUMSIZE32);
	case Qimage:
		assert(v);
		print_func_exit();
		return readmem(offset, ubuf, n, v->image, v->imagesize);
	default:
		panic("Bad QID %p in devnix", c->qid.path);
	}
	print_func_exit();
	return 0;
}

static long nixwrite(struct chan *c, void *ubuf, long n, int64_t off)
{
	struct nix *v = c->aux;
	print_func_entry();
	ERRSTACK(3);
	char buf[32];
	struct cmdbuf *cb;
	struct nix *nix;
	uint64_t hexval;
	printd("nixwrite(%p, %p, %d)\n", c, ubuf, n);
	switch (TYPE(c->qid)) {
	case Qtopdir:
	case Qnixdir:
	case Qstat:
		error(Eperm);
	case Qctl:
		nix = c->aux;
		printk("qctl: nix is %p, nix is %p\n", nix, nix);
		cb = parsecmd(ubuf, n);
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
			if (!allcores[core].usable)
				error("Bad core %d", core);
			allcores[core].assignment = (void *)ip;
			wmb();
			printk("nix_run returns \n");
			print_func_exit();
		} else if (!strcmp(cb->f[0], "test")) {
			int core;
			if (cb->nf != 2)
				error("usage: test core");
			core = strtoul(cb->f[1], 0, 0);
			if (!allcores[core].usable)
				error("Bad core %d", core);
			allcores[core].assignment = nixtest;
			wmb();
			printk("nix_run returns \n");
			print_func_exit();
		} else if (!strcmp(cb->f[0], "check")) {
			int i;
			for(i = 0; i < ARRAY_SIZE(allcores); i++) {
				if (! allcores[i].usable)
					continue;
				printk("%p %p %p %p %d %d\n",
				       allcores[i].assignment,
				       allcores[i].work,
				       allcores[i].result,
				       allcores[i].done,
				       allcores[i].usable,
				       allcores[i].state);
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
		printd("copy to %p ubuf %p size %d\n", v->image + off, ubuf, n);

		if (memcpy_from_user_errno(current, v->image + off, ubuf, n) < 0)
			error("%s: bad user addr %p", __FUNCTION__, ubuf);
		break;

	default:
		panic("Bad QID %p in devnix", c->qid.path);
	}
	print_func_exit();
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
