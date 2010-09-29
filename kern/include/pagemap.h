/* Copyright (c) 2010 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Page mapping: maps an object (inode or block dev) in page size chunks.
 * Analagous to Linux's "struct address space".  While this is closely coupled
 * with the VFS, block devices also use it (hence the separate header and c
 * file). */

#ifndef ROS_KERN_PAGEMAP_H
#define ROS_KERN_PAGEMAP_H

#include <radix.h>
#include <atomic.h>

/* Need to be careful, due to some ghetto circular references */
struct page;
struct inode;
struct block_device;
struct page_map_operations;

/* Every object that has pages, like an inode or the swap (or even direct block
 * devices) has a page_map, tracking which of its pages are currently in memory.
 * It is a map, per object, from index to physical page frame. */
struct page_map {
	union {
		struct inode				*pm_host;	/* inode of the owner, if any */
		struct block_device			*pm_bdev;	/* bdev of the owner, if any */
	};
	struct radix_tree			pm_tree;		/* tracks present pages */
	spinlock_t					pm_tree_lock;	/* spinlock => we can't block */
	unsigned long				pm_num_pages;	/* how many pages are present */
	struct page_map_operations	*pm_op;
	unsigned int				pm_flags;
	/*... and private lists, backing block dev info, other mappings, etc. */
};

/* Operations performed on a page_map.  These are usually FS specific, which
 * get assigned when the inode is created.
 * Will fill these in as they are created/needed/used. */
struct page_map_operations {
	int (*readpage) (struct page_map *, struct page *);
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
struct page *pm_find_page(struct page_map *pm, unsigned long index);
int pm_insert_page(struct page_map *pm, unsigned long index, struct page *page);
int pm_remove_page(struct page_map *pm, struct page *page);
int pm_load_page(struct page_map *pm, unsigned long index, struct page **pp);

#endif /* ROS_KERN_PAGEMAP_H */
