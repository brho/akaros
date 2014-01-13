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
static struct page *pm_find_page(struct page_map *pm, unsigned long index)
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
static int pm_insert_page(struct page_map *pm, unsigned long index,
                          struct page *page)
{
	int error = 0;
	spin_lock(&pm->pm_tree_lock);
	error = radix_insert(&pm->pm_tree, index, page);
	if (!error) {
		page_incref(page);
		/* setting PG_BUF since we know it'll be used for IO later... */
		atomic_or(&page->pg_flags, PG_LOCKED | PG_BUFFER | PG_PAGEMAP);
		page->pg_sem.nr_signals = 0;		/* ensure others will block */
		page->pg_mapping = pm;
		page->pg_index = index;
		pm->pm_num_pages++;
	}
	spin_unlock(&pm->pm_tree_lock);
	return error;
}

void pm_put_page(struct page *page)
{
	page_decref(page);
}

/* Makes sure the index'th page of the mapped object is loaded in the page cache
 * and returns its location via **pp.  Note this will give you a refcnt'd
 * reference to the page.  This may block! TODO: (BLK) */
int pm_load_page(struct page_map *pm, unsigned long index, struct page **pp)
{
	struct page *page;
	int error;
	bool page_was_mapped = TRUE;

	page = pm_find_page(pm, index);
	while (!page) {
		/* kpage_alloc, since we want the page to persist after the proc
		 * dies (can be used by others, until the inode shuts down). */
		if (kpage_alloc(&page))
			return -ENOMEM;
		/* might want to initialize other things, perhaps in page_alloc() */
		atomic_set(&page->pg_flags, 0);
		error = pm_insert_page(pm, index, page);
		switch (error) {
			case 0:
				page_was_mapped = FALSE;
				break;
			case -EEXIST:
				/* the page was mapped already (benign race), just get rid of
				 * our page and try again (the only case that uses the while) */
				page_decref(page);
				page = pm_find_page(pm, index);
				break;
			default:
				/* something is wrong, bail out! */
				page_decref(page);
				return error;
		}
	}
	assert(page && kref_refcnt(&page->pg_kref));
	/* At this point, page is a refcnt'd page, and we return the reference.
	 * Also, there's an unlikely race where we're not in the page cache anymore,
	 * and this all is useless work. */
	*pp = page;
	/* if the page was in the map, we need to do some checks, and might have to
	 * read in the page later.  If the page was freshly inserted to the pm by
	 * us, we skip this since we are the one doing the readpage(). */
	if (page_was_mapped) {
		/* is it already here and up to date?  if so, we're done */
		if (atomic_read(&page->pg_flags) & PG_UPTODATE)
			return 0;
		/* if not, try to lock the page (could BLOCK) */
		lock_page(page);
		/* we got it, is our page still in the cache?  check the mapping.  if
		 * not, start over, perhaps with EAGAIN and outside support */
		if (!page->pg_mapping)
			panic("Page is not in the mapping!  Haven't implemented this!");
		/* double check, are we up to date?  if so, we're done */
		if (atomic_read(&page->pg_flags) & PG_UPTODATE) {
			unlock_page(page);
			return 0;
		}
	}
	/* if we're here, the page is locked by us, and it needs to be read in */
	assert(page->pg_mapping == pm);
	/* Readpage will block internally, returning when it is done */
	error = pm->pm_op->readpage(pm, page);
	assert(!error);
	/* Unlock, since we're done with the page and it is up to date */
	unlock_page(page);
	assert(atomic_read(&page->pg_flags) & PG_UPTODATE);
	return 0;
}
