/* Copyright (c) 2010 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Page mapping: maps an object (inode or block dev) in page size chunks.
 * Analagous to Linux's "struct address space".  While this is closely coupled
 * with the VFS, block devices also use it (hence the separate header and c
 * file). */

#pragma once

#include <radix.h>
#include <atomic.h>
#include <mm.h>

/* Need to be careful, due to some ghetto circular references */
struct page;
struct chan;
struct page_map_operations;

/* Every object that has pages has a page_map, tracking which of its pages are
 * currently in memory.  It is a map, per object, from index to physical page
 * frame. */
struct page_map {
	qlock_t						pm_qlock;		/* for the radix tree nr_pgs */
	struct fs_file				*pm_file;
	struct radix_tree			pm_tree;		/* tracks present pages */
	unsigned long				pm_num_pages;	/* how many pages are present */
	struct page_map_operations	*pm_op;
	spinlock_t					pm_lock;		/* for the VMR list */
	struct vmr_tailq			pm_vmrs;
};

/* Operations performed on a page_map.  These are usually FS specific, which
 * get assigned when the inode is created.
 * Will fill these in as they are created/needed/used. */
struct page_map_operations {
	int (*readpage) (struct page_map *, struct page *);
	int (*writepage) (struct page_map *, struct page *);
/*	readpages: read a list of pages
	writepage: write from a page to its backing store
	writepages: write a list of pages
	sync_page: start the IO of already scheduled ops
	set_page_dirty: mark the given page dirty
	prepare_write: prepare to write (disk backed pages)
	commit_write: complete a write (disk backed pages)
	bmap: get a logical block number from a file block index
	invalidate page: invalidate, part of truncating
	release page: prepare to release
	direct_io: bypass the page cache */
};

/* Page cache functions */
void pm_init(struct page_map *pm, struct page_map_operations *op, void *host);
int pm_load_page(struct page_map *pm, unsigned long index, struct page **pp);
int pm_load_page_nowait(struct page_map *pm, unsigned long index,
                        struct page **pp);
void pm_put_page(struct page *page);
void pm_add_vmr(struct page_map *pm, struct vm_region *vmr);
void pm_remove_vmr(struct page_map *pm, struct vm_region *vmr);
int pm_remove_contig(struct page_map *pm, unsigned long index,
                     unsigned long nr_pgs);
void pm_remove_or_zero_pages(struct page_map *pm, unsigned long start_idx,
                             unsigned long nr_pgs);
void pm_destroy(struct page_map *pm);
void pm_page_asserter(struct page *page, char *str);
void print_page_map_info(struct page_map *pm);
