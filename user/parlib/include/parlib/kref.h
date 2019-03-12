/* Copyright (c) 2010 The Regents of the University of California
 * Copyright (c) 2016 Google Inc
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Parlib's version of krefs. */

#pragma once

#include <stdint.h>
#include <assert.h>
#include <ros/common.h>

struct kref {
	unsigned int refcnt;
	void (*release)(struct kref *kref);
};

static void kref_init(struct kref *kref, void (*release)(struct kref *kref),
                      unsigned int init_ref)
{
	kref->refcnt = init_ref;
	kref->release = release;
}

/* Returns TRUE if successful. */
static bool kref_get_not_zero(struct kref *kref, unsigned int inc)
{
	unsigned int old_ref, new_ref;

	do {
		old_ref = ACCESS_ONCE(kref->refcnt);
		if (!old_ref)
			return FALSE;
		new_ref = old_ref + inc;
	} while (!__sync_bool_compare_and_swap(&kref->refcnt, old_ref,
					       new_ref));
	return TRUE;
}

static void kref_get(struct kref *kref, unsigned int inc)
{
	assert(kref->refcnt);
	__sync_fetch_and_add(&kref->refcnt, inc);
}

/* Returns TRUE if we released */
static bool kref_put(struct kref *kref)
{
	unsigned int old_ref;

	assert(kref->refcnt);
	old_ref = __sync_sub_and_fetch(&kref->refcnt, 1);
	if (old_ref)
		return FALSE;
	kref->release(kref);
	return TRUE;
}
