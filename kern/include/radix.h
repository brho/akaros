/* Copyright (c) 2010 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Radix Tree, modeled after Linux's (described here:
 * http://lwn.net/Articles/175432/).
 *
 * It maps unsigned longs to void*s, growing the depth of the tree as needed to
 * handle more items.  It won't allow the insertion of existing keys, and it
 * can fail due to lack of memory.
 *
 * There are some utility functions, probably unimplemented til we need them,
 * that will make the tree have enough memory for future calls.
 *
 * You can also store a tag along with the void* for a given item, and do
 * lookups based on those tags.  Or you will be able to, once it is
 * implemented. */

#pragma once

#define LOG_RNODE_SLOTS 6
#define NR_RNODE_SLOTS (1 << LOG_RNODE_SLOTS)

#include <ros/common.h>
#include <kthread.h>
#include <atomic.h>
#include <rcu.h>

struct radix_node {
	struct rcu_head				rcu;
	void						*items[NR_RNODE_SLOTS];
	unsigned int				num_items;
	bool						leaf;
	struct radix_node			*parent;
	struct radix_node			**my_slot;
};

/* writers (insert, delete, and callbacks) must synchronize externally, e.g. a
 * qlock in the pagemap.
 *
 * radix_lookup_slot returns an rcu-protected pointer that needs to be
 * rcu_read_locked.  The item in the slot (either by *slot or by a regular
 * radix_lookup) has limited protections.  Higher layers (pagemap) can do their
 * own thing.  For instance, the page cache writers can zero an item if they
 * know they cleared the page without someone else grabbing a ref.  We'll
 * rcu-protect the item pointer, so that higher layers can use RCU for the
 * actual object.
 *
 * Basically the only functions that don't need the caller to hold a qlock are
 * the two lookup routines: the readers.  The seq counter protects the atomicity
 * of root, depth, and upper_bound, which defines the reader's start point.  RCU
 * protects the lifetime of the rnodes, which is where lookup_slot's pointer
 * points to.
 *
 * Note that we use rcu_assign_pointer and rcu_dereference whenever we
 * manipulate pointers in the tree (pointers to or within rnodes).  We use RCU
 * for the item pointer too, so that our callers can use RCU if they want.  Both
 * the slot pointer and what it points to are protected by RCU.
 */
struct radix_tree {
	seq_ctr_t					seq;
	struct radix_node			*root;
	unsigned int				depth;
	unsigned long				upper_bound;
};

void radix_init(void);		/* initializes the whole radix system */
void radix_tree_init(struct radix_tree *tree);	/* inits one tree */
void radix_tree_destroy(struct radix_tree *tree);

/* Item management */
int radix_insert(struct radix_tree *tree, unsigned long key, void *item,
                 void ***slot_p);
void *radix_delete(struct radix_tree *tree, unsigned long key);
void *radix_lookup(struct radix_tree *tree, unsigned long key);
void **radix_lookup_slot(struct radix_tree *tree, unsigned long key);

typedef bool (*radix_cb_t)(void **slot, unsigned long tree_idx, void *arg);
void radix_for_each_slot(struct radix_tree *tree, radix_cb_t cb, void *arg);
/* [start_idx, end_idx) */
void radix_for_each_slot_in_range(struct radix_tree *tree,
                                  unsigned long start_idx,
                                  unsigned long end_idx,
                                  radix_cb_t cb, void *arg);

/* Memory management */
int radix_grow(struct radix_tree *tree, unsigned long max);
int radix_preload(struct radix_tree *tree, int flags);

/* Tag management */
void *radix_tag_set(struct radix_tree *tree, unsigned long key, int tag);
void *radix_tag_clear(struct radix_tree *tree, unsigned long key, int tag);
int radix_tag_get(struct radix_tree *tree, unsigned long key, int tag);
int radix_tree_tagged(struct radix_tree *tree, int tag);
int radix_tag_gang_lookup(struct radix_tree *tree, void **results,
                          unsigned long first, unsigned int max_items, int tag);

/* Debugging */
void print_radix_tree(struct radix_tree *tree);
