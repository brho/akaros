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
 * See our Documentation/kref.txt for more info. */

#ifndef ROS_KERN_KREF_H
#define ROS_KERN_KREF_H

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

/* Helper for some debugging situations */
static long kref_refcnt(struct kref *kref)
{
	return atomic_read(&kref->refcount);
}

static void kref_init(struct kref *kref, void (*release)(struct kref *kref),
                      unsigned int init)
{
	assert(release);
	atomic_init(&kref->refcount, init);
	kref->release = release;
}

/* Will blindly incref */
static struct kref *__kref_get(struct kref *kref, unsigned int inc)
{
	atomic_add(&kref->refcount, inc);
	return kref;
}

/* Returns the kref ptr on success, 0 on failure */
static struct kref *kref_get_not_zero(struct kref *kref, unsigned int inc)
{
	if (atomic_add_not_zero(&kref->refcount, inc))
		return kref;
	else
		return 0;
}

/* Will panic on zero */
static struct kref *kref_get(struct kref *kref, unsigned int inc)
{
	kref = kref_get_not_zero(kref, inc);
	assert(kref);
	return kref;
}

/* Returns True if we hit 0 and executed 'release', False otherwise */
static bool kref_put(struct kref *kref)
{
	assert(kref_refcnt(kref) > 0);		/* catch some bugs */
	if (atomic_sub_and_test(&kref->refcount, 1)) {
		kref->release(kref);
		return TRUE;
	}
	return FALSE;
}

/* Dev / debugging function to catch the attempted freeing of objects we don't
 * know how to free yet. */
static void fake_release(struct kref *kref)
{
	panic("Cleaning up this object is not supported!\n");
}

#endif /* ROS_KERN_KREF_H */
