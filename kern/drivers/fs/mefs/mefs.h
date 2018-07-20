/* Copyright (c) 2018 Google Inc
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 */

#pragma once

#include <sys/types.h>
#include <ros/arch/mmu.h>
#include <sys/queue.h>

#define MEFS_BTAG_FREE		1
#define MEFS_BTAG_ALLOC		2

/* all_link is rooted at all_segs in the SB.  misc_link is used for the
 * unused_btags list (btag cache) or the free seg list. */
struct mefs_btag {
	BSD_LIST_ENTRY(mefs_btag)	all_link;
	BSD_LIST_ENTRY(mefs_btag)	misc_link;
	uintptr_t					start;
	size_t						size;
	int							status;
};
BSD_LIST_HEAD(mefs_btag_list, mefs_btag);

/* 64 is the most powers of two we can express with 64 bits. */
#define MEFS_NR_FREE_LISTS		64
#define MEFS_QUANTUM			PGSIZE
#define MEFS_MAGIC				"MEFS001"

/* all_segs is the sorted list of all btags that cover the memory space.  i.e.
 * not the unused btags, but all the btags for the allocated and free memory. */
struct mefs_superblock {
	char						magic[8];
	struct mefs_btag_list		all_segs;
	struct mefs_btag_list		unused_btags;
	struct mefs_btag_list		free_segs[MEFS_NR_FREE_LISTS];
	size_t						amt_total_segs;
	size_t						amt_alloc_segs;
};

struct mefs_superblock *mefs_super_create(uintptr_t init_seg, size_t size);
struct mefs_superblock *mefs_super_attach(uintptr_t init_seg, size_t size);
void mefs_super_add(struct mefs_superblock *sb, uintptr_t seg, size_t size);
void mefs_super_destroy(struct mefs_superblock *sb);
void mefs_super_dump(struct mefs_superblock *sb);
struct mefs_btag *mefs_ext_alloc(struct mefs_superblock *sb, size_t size);
void mefs_ext_free(struct mefs_superblock *sb, struct mefs_btag *bt);
