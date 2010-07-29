/* Copyright (c) 2010 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Kernel reference counting, based on Linux's kref:
 * - http://www.kroah.com/linux/talks/ols_2004_kref_paper/
 *          Reprint-Kroah-Hartman-OLS2004.pdf
 * - http://lwn.net/Articles/336224/
 * - Linux's Documentation/kref.txt
 *
 * We differ a bit in that we currently ref count items that are on lists.  If
 * an item is stored on a list, that counts as a reference.  No need to lock
 * around kref_put, nor do you need to kref_get your list reference *if* you
 * take the reference out of the list.  You need to kref_get() (if you want to
 * use the reference later) before allowing someone else access to the list,
 * which is still IAW Linux's style.  They might even do this for some lists. If
 * we have lists that are unsynchronized where two threads can get references to
 * the same item at the same time, then we'll need to lock to deal with that.
 *
 * We also allow incrementing by more than one, which helps in some cases.  We
 * don't allow decrementing by more than one to catch bugs (for now).
 *
 * As far as usage goes, kref users don't make much of a distinction between
 * internal and external references yet.
 *
 * kref rules (paraphrasing the linux ones):
 * 1. If you pass a pointer somewhere or store it, kref_get() it first.  You can
 * do this with no locking if you have a valid reference.
 * 2. When you are done, kref_put() it.  You can usually do this without
 * locking.
 * 3. If you never kref_get without already holding a valid reference, you don't
 * need to lock for Rule 2.  If you ever grab a reference without already having
 * one, you need some form of sync to prevent a kref_put() from happening
 * while you kref_get().
 *
 * The closest we get to mucking with it is with the proc hash table, though we
 * don't require a lock on every proc kref_put().  If you're
 * curious about these sorts of things, note how easy it is for a list where you
 * are added or removed (like the runnable list) compared to a structure where
 * we make a copy of the reference (like the pid2proc hash). */

#include <atomic.h>
#include <assert.h>

/* Current versions of Linux pass in 'release' on the kref_put() callsite to
 * save on space in whatever struct these are embedded in.  We don't, since it's
 * a little more complicated and we will probably change the release function a
 * lot for subsystems in development. */
struct kref {
	atomic_t refcount;
	void (*release)(struct kref *kref);
};

static void kref_init(struct kref *kref, void (*release)(struct kref *kref),
                      unsigned int init)
{
	assert(release);
	atomic_init(&kref->refcount, init);
	kref->release = release;
}

static struct kref *kref_get(struct kref *kref, unsigned int inc)
{
	assert(atomic_read(&kref->refcount));
	atomic_add(&kref->refcount, inc);
	return kref;
}

static void kref_put(struct kref *kref)
{
	assert(atomic_read(&kref->refcount) > 0);		/* catch some bugs */
	if (atomic_sub_and_test(&kref->refcount, 1))
		kref->release(kref);
}
