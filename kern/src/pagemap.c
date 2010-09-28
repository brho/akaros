/* Copyright (c) 2010 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Page mapping: maps an object (inode or block dev) in page size chunks.
 * Analagous to Linux's "struct address space" */

#include <pmap.h>
#include <atomic.h>
#include <radix.h>
#include <kref.h>
#include <assert.h>
#include <stdio.h>

/* Initializes a PM.  Host should be an *inode or a *bdev (doesn't matter).  The
 * reference this stores is uncounted. */
void pm_init(struct page_map *pm, struct page_map_operations *op, void *host)
{
	pm->pm_bdev = host;						/* note the uncounted ref */
	radix_tree_init(&pm->pm_tree);
	spinlock_init(&pm->pm_tree_lock);
	pm->pm_num_pages = 0;					/* no pages in a new pm */
	pm->pm_op = op;
	pm->pm_flags = 0;
}

/* Looks up the index'th page in the page map, returning an incref'd reference,
 * or 0 if it was not in the map. */
struct page *pm_find_page(struct page_map *pm, unsigned long index)
{
	spin_lock(&pm->pm_tree_lock);
	struct page *page = (struct page*)radix_lookup(&pm->pm_tree, index);
	if (page)
		page_incref(page);
	spin_unlock(&pm->pm_tree_lock);
	return page;
}

/* Attempts to insert the page into the page_map, returns 0 for success, or an
 * error code if there was one already (EEXIST) or we ran out of memory
 * (ENOMEM).  On success, this will preemptively lock the page, and will also
 * store a reference to the page in the pm. */
int pm_insert_page(struct page_map *pm, unsigned long index, struct page *page)
{
	int error = 0;
	spin_lock(&pm->pm_tree_lock);
	error = radix_insert(&pm->pm_tree, index, page);
	if (!error) {
		page_incref(page);
		page->pg_flags |= PG_LOCKED | PG_BUFFER;
		page->pg_mapping = pm;
		page->pg_index = index;
		pm->pm_num_pages++;
	}
	spin_unlock(&pm->pm_tree_lock);
	return error;
}

/* Removes the page, including its reference.  Not sure yet what interface we
 * want to this (pm and index or page), and this has never been used.  There are
 * also issues with when you want to call this, since a page in the cache may be
 * mmap'd by someone else. */
int pm_remove_page(struct page_map *pm, struct page *page)
{
	void *retval;
	warn("pm_remove_page() hasn't been thought through or tested.");
	/* TODO: check for dirty pages, don't let them be removed right away.  Need
	 * to schedule them for writeback, and then remove them later (callback). */
	spin_lock(&pm->pm_tree_lock);
	retval = radix_delete(&pm->pm_tree, page->pg_index);
	spin_unlock(&pm->pm_tree_lock);
	assert(retval == (void*)page);
	page_decref(page);
	pm->pm_num_pages--;
	return 0;
}
