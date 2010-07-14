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

#ifndef ROS_KERN_RADIX_H
#define ROS_KERN_RADIX_H

#define LOG_RNODE_SLOTS 6
#define NR_RNODE_SLOTS (1 << LOG_RNODE_SLOTS)

#include <ros/common.h>

struct radix_node {
	void						*items[NR_RNODE_SLOTS];
	unsigned int				num_items;
	bool						leaf;
	struct radix_node			*parent;
	struct radix_node			**my_slot;
};

/* Defines the whole tree. */
struct radix_tree {
	struct radix_node			*root;
	unsigned int				depth;
	unsigned long				upper_bound;
};

void radix_init(void);		/* initializes the whole radix system */
#define RADIX_INITIALIZER {0, 0, 0}
void radix_tree_init(struct radix_tree *tree);	/* inits one tree */
void radix_tree_destroy(struct radix_tree *tree);

/* Item management */
int radix_insert(struct radix_tree *tree, unsigned long key, void *item);
void *radix_delete(struct radix_tree *tree, unsigned long key);
void *radix_lookup(struct radix_tree *tree, unsigned long key);
void **radix_lookup_slot(struct radix_tree *tree, unsigned long key);
int radix_gang_lookup(struct radix_tree *tree, void **results,
                      unsigned long first, unsigned int max_items);

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

#endif /* !ROS_KERN_RADIX_H */
