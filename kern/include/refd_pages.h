/* Copyright (c) 2016 Google Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Helpers for reference counted pages, for use with Linux code.
 *
 * Some code wants to use reference counted pages.  I'd like to keep these
 * uses separate from the main memory allocator.  Code that wants reference
 * counted pages can use these helpers.
 *
 * Pass in memory allocated with get_cont_pages(). */

#pragma once

#include <kref.h>
#include <page_alloc.h>
#include <pmap.h>
#include <kmalloc.h>
#include <assert.h>

struct refd_pages {
	void				*rp_kva;
	size_t				rp_order;
	struct kref			rp_kref;
};

static struct page *rp2page(struct refd_pages *rp)
{
	return kva2page(rp->rp_kva);
}

static void refd_pages_release(struct kref *kref)
{
	struct refd_pages *rp = container_of(kref, struct refd_pages, rp_kref);

	free_cont_pages(rp->rp_kva, rp->rp_order);
	kfree(rp);
}

static struct refd_pages *get_refd_pages(void *kva, size_t order)
{
	struct refd_pages *rp;

	if (!kva)
		return 0;
	rp = kmalloc(sizeof(struct refd_pages), MEM_WAIT);
	assert(rp);
	rp->rp_kva = kva;
	rp->rp_order = order;
	kref_init(&rp->rp_kref, refd_pages_release, 1);
	return rp;
}

static void refd_pages_decref(struct refd_pages *rp)
{
	kref_put(&rp->rp_kref);
}
