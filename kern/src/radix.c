/* Copyright (c) 2010 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Radix Trees!  Just the basics, doesn't do tagging or anything fancy. */

#include <ros/errno.h>
#include <radix.h>
#include <slab.h>
#include <kmalloc.h>
#include <string.h>
#include <stdio.h>
#include <rcu.h>

struct kmem_cache *radix_kcache;
static struct radix_node *__radix_lookup_node(struct radix_tree *tree,
                                              unsigned long key,
                                              bool extend);
static void __radix_remove_slot(struct radix_node *r_node, struct radix_node **slot);

/* Initializes the radix tree system, mostly just builds the kcache */
void radix_init(void)
{
	radix_kcache = kmem_cache_create("radix_nodes",
					 sizeof(struct radix_node),
					 __alignof__(struct radix_node), 0,
					 NULL, 0, 0, NULL);
}

/* Initializes a tree dynamically */
void radix_tree_init(struct radix_tree *tree)
{
	tree->seq = SEQCTR_INITIALIZER;
	tree->root = 0;
	tree->depth = 0;
	tree->upper_bound = 0;
}

static bool __should_not_run_cb(void **slot, unsigned long tree_idx, void *a)
{
	panic("Tried destroying a non-empty tree! (slot %p, idx %lu)",
	      *slot, tree_idx);
}

/* Will clean up all the memory associated with a tree.  Shouldn't be necessary
 * if you delete all of the items, which you should do anyways since they are
 * usually void*.  Might expand this to have a function to call on every leaf
 * slot. */
void radix_tree_destroy(struct radix_tree *tree)
{
	/* Currently, we may have a root node, even if all the elements were removed
	 */
	radix_for_each_slot(tree, __should_not_run_cb, NULL);
	if (tree->root) {
		kmem_cache_free(radix_kcache, tree->root);
		tree->root = NULL;
	}
}

/* Attempts to insert an item in the tree at the given key.  ENOMEM if we ran
 * out of memory, EEXIST if an item is already in the tree.  On success, will
 * also return the slot pointer, if requested.
 *
 * Caller must maintain mutual exclusion (qlock) */
int radix_insert(struct radix_tree *tree, unsigned long key, void *item,
                 void ***slot_p)
{
	struct radix_node *r_node;
	void **slot;

	/* Is the tree tall enough?  if not, it needs to grow a level.  This will
	 * also create the initial node (upper bound starts at 0). */
	while (key >= tree->upper_bound) {
		r_node = kmem_cache_alloc(radix_kcache, MEM_WAIT);
		memset(r_node, 0, sizeof(struct radix_node));
		if (tree->root) {
			/* tree->root is the old root, now a child of the future root */
			r_node->items[0] = tree->root;
			tree->root->parent = r_node;
			tree->root->my_slot = (struct radix_node**)&r_node->items[0];
			r_node->num_items = 1;
		} else {
			/* if there was no root before, we're both the root and a leaf */
			r_node->leaf = TRUE;
			r_node->parent = 0;
		}
		/* Need to atomically change root, depth, and upper_bound for our
		 * readers, who will check the seq ctr. */
		__seq_start_write(&tree->seq);
		tree->root = r_node;
		r_node->my_slot = &tree->root;
		tree->depth++;
		tree->upper_bound = 1ULL << (LOG_RNODE_SLOTS * tree->depth);
		__seq_end_write(&tree->seq);
	}
	assert(tree->root);
	/* the tree now thinks it is tall enough, so find the last node, insert in
	 * it, etc */
	/* This gives us an rcu-protected pointer, though we hold the lock. */
	r_node = __radix_lookup_node(tree, key, TRUE);
	assert(r_node);		/* we want an ENOMEM actually, but i want to see this */
	slot = &r_node->items[key & (NR_RNODE_SLOTS - 1)];
	if (*slot)
		return -EEXIST;
	rcu_assign_pointer(*slot, item);
	r_node->num_items++;
	if (slot_p)
		*slot_p = slot;	/* passing back an rcu-protected pointer */
	return 0;
}

static void __rnode_free_rcu(struct rcu_head *head)
{
	struct radix_node *r_node = container_of(head, struct radix_node, rcu);

	kmem_cache_free(radix_kcache, r_node);
}

/* Removes an item from it's parent's structure, freeing the parent if there is
 * nothing left, potentially recursively. */
static void __radix_remove_slot(struct radix_node *r_node,
                                struct radix_node **slot)
{
	assert(*slot);		/* make sure there is something there */
	rcu_assign_pointer(*slot, NULL);
	r_node->num_items--;
	/* this check excludes the root, but the if else handles it.  For now, once
	 * we have a root, we'll always keep it (will need some changing in
	 * radix_insert() */
	if (!r_node->num_items && r_node->parent) {
		if (r_node->parent)
			__radix_remove_slot(r_node->parent, r_node->my_slot);
		else			/* we're the last node, attached to the actual tree */
			*(r_node->my_slot) = 0;
		call_rcu(&r_node->rcu, __rnode_free_rcu);
	}
}

/* Removes a key/item from the tree, returning that item (the void*).  If it
 * detects a radix_node is now unused, it will dealloc that node.  Though the
 * tree will still think it is tall enough to handle its old upper_bound.  It
 * won't "shrink".
 *
 * Caller must maintain mutual exclusion (qlock) */
void *radix_delete(struct radix_tree *tree, unsigned long key)
{
	void **slot;
	void *retval;
	struct radix_node *r_node;

	/* This is an rcu-protected pointer, though the caller holds a lock. */
	r_node = __radix_lookup_node(tree, key, false);
	if (!r_node)
		return 0;
	slot = &r_node->items[key & (NR_RNODE_SLOTS - 1)];
	retval = rcu_dereference(*slot);
	if (retval) {
		__radix_remove_slot(r_node, (struct radix_node**)slot);
	} else {
		/* it's okay to delete an empty, but i want to know about it for now */
		warn("Tried to remove a non-existant item from a radix tree!");
	}
	return retval;
}

/* Returns the item for a given key.  0 means no item, etc. */
void *radix_lookup(struct radix_tree *tree, unsigned long key)
{
	void **slot = radix_lookup_slot(tree, key);

	if (!slot)
		return 0;
	/* slot was rcu-protected, pointing into the memory of an r_node.  we also
	 * want *slot, which is "void *item" to be an rcu-protected pointer. */
	return rcu_dereference(*slot);
}

/* Returns a pointer to the radix_node holding a given key.  0 if there is no
 * such node, due to the tree being too small or something.
 *
 * If the depth is greater than one, we need to walk down the tree a level.  The
 * key is 'partitioned' among the levels of the tree, like so:
 * ......444444333333222222111111
 *
 * If an interior node of the tree is missing, this will add one if it was
 * directed to extend the tree.
 *
 * If we might extend, the caller must maintain mutual exclusion (qlock) */
static struct radix_node *__radix_lookup_node(struct radix_tree *tree,
                                              unsigned long key, bool extend)
{
	unsigned long idx, upper_bound;
	unsigned int depth;
	seq_ctr_t seq;
	struct radix_node *child_node, *r_node;

	do {
		seq = ACCESS_ONCE(tree->seq);
		rmb();
		r_node = rcu_dereference(tree->root);
		depth = tree->depth;
		upper_bound = tree->upper_bound;
	} while (seqctr_retry(tree->seq, seq));

	if (key	>= upper_bound) {
		if (extend)
			warn("Bound (%d) not set for key %d!\n", upper_bound, key);
		return 0;
	}
	for (int i = depth; i > 1; i--) {	 /* i = ..., 4, 3, 2 */
		idx = (key >> (LOG_RNODE_SLOTS * (i - 1))) & (NR_RNODE_SLOTS - 1);
		/* There might not be a node at this part of the tree */
		if (!r_node->items[idx]) {
			if (!extend)
				return 0;
			child_node = kmem_cache_alloc(radix_kcache, MEM_WAIT);
			memset(child_node, 0, sizeof(struct radix_node));
			/* when we are on the last iteration (i == 2), the child will be
			 * a leaf. */
			child_node->leaf = (i == 2) ? TRUE : FALSE;
			child_node->parent = r_node;
			child_node->my_slot = (struct radix_node**)&r_node->items[idx];
			r_node->num_items++;
			rcu_assign_pointer(r_node->items[idx], child_node);
		}
		r_node = (struct radix_node*)rcu_dereference(r_node->items[idx]);
	}
	return r_node;
}

/* Returns a pointer to the slot for the given key.  0 if there is no such slot,
 * etc */
void **radix_lookup_slot(struct radix_tree *tree, unsigned long key)
{
	struct radix_node *r_node = __radix_lookup_node(tree, key, FALSE);

	if (!r_node)
		return 0;
	key = key & (NR_RNODE_SLOTS - 1);
	/* r_node is rcu-protected.  Our retval is too, since it's a pointer into
	 * the same object as r_node. */
	return &r_node->items[key];
}

/* [x_left, x_right) and [y_left, y_right). */
static bool ranges_overlap(unsigned long x_left, unsigned long x_right,
                           unsigned long y_left, unsigned long y_right)
{
	return ((x_left <= y_left) && (y_left < x_right)) ||
	       ((y_left <= x_left) && (x_left < y_right));
}

/* Given an index at a depth for a child, returns whether part of it is in the
 * global range.
 *
 * Recall the key is partitioned like so: ....444444333333222222111111.  The
 * depth is 1 when we're in the last rnode and our children are items.  When
 * we're an intermediate node, our depth is higher, and our start/end is the
 * entire reach of us + our children. */
static bool child_overlaps_range(unsigned long idx, int depth,
                                 unsigned long glb_start_idx,
                                 unsigned long glb_end_idx)
{
	unsigned long start = idx << (LOG_RNODE_SLOTS * (depth - 1));
	unsigned long end = (idx + 1) << (LOG_RNODE_SLOTS * (depth - 1));

	return ranges_overlap(start, end, glb_start_idx, glb_end_idx);
}

/* Helper for walking down a tree.
 * - depth == 1 means we're on the last radix_node, whose items are all the
 *   actual items.
 * - tree_idx is the index for our item/node.  It encodes the path we took
 *   through the radix tree to get to where we are.  For leaves (items), it is
 *   their index in the address space.  For internal rnodes, it is a temporary
 *   number, but combined with the depth, you can determine the range of the
 *   rnode's descendents.
 * - glb_start_idx and glb_end_idx is the global start and end for the entire
 *   for_each operation.
 *
 * Returns true if our r_node *was already deleted*.  When we call
 * __radix_remove_slot(), if we removed the last item for r_node, the removal
 * code will recurse *up* the tree, such that r_node might already be freed.
 * Same goes for our parent!  Hence, we're careful to only access r_node when we
 * know we have children (once we enter the loop and might remove a slot). */
static bool rnode_for_each(struct radix_node *r_node, int depth,
                           unsigned long tree_idx, unsigned long glb_start_idx,
                           unsigned long glb_end_idx,
                           radix_cb_t cb, void *arg)
{
	unsigned int num_children = ACCESS_ONCE(r_node->num_items);

	/* The tree_idx we were passed was from our parent's perspective.  We need
	 * shift it over each time we walk down to put it in terms of our
	 * level/depth.  Or think of it as making room for our bits (the values of
	 * i). */
	tree_idx <<= LOG_RNODE_SLOTS;
	for (int i = 0; num_children && (i < NR_RNODE_SLOTS); i++) {
		if (r_node->items[i]) {
			/* If we really care, we can try to abort the rest of the loop.  Not
			 * a big deal */
			if (!child_overlaps_range(tree_idx + i, depth, glb_start_idx,
			                          glb_end_idx))
				continue;
			if (depth > 1) {
				if (rnode_for_each(r_node->items[i], depth - 1, tree_idx + i,
				                   glb_start_idx, glb_end_idx, cb, arg))
					num_children--;
			} else {
				if (cb(&r_node->items[i], tree_idx + i, arg)) {
					__radix_remove_slot(r_node,
					                    (struct radix_node**)&r_node->items[i]);
					num_children--;
				}
			}
		}
	}
	return num_children ? false : true;
}

/* [start_idx, end_idx).
 *
 * Caller must maintain mutual exclusion (qlock) */
void radix_for_each_slot_in_range(struct radix_tree *tree,
                                  unsigned long start_idx,
                                  unsigned long end_idx,
                                  radix_cb_t cb, void *arg)
{
	if (!tree->root)
		return;
	rnode_for_each(tree->root, tree->depth, 0, start_idx, end_idx, cb, arg);
}

/* Caller must maintain mutual exclusion (qlock) */
void radix_for_each_slot(struct radix_tree *tree, radix_cb_t cb, void *arg)
{
	radix_for_each_slot_in_range(tree, 0, ULONG_MAX, cb, arg);
}

int radix_gang_lookup(struct radix_tree *tree, void **results,
                      unsigned long first, unsigned int max_items)
{
	panic("Not implemented");
	return -1; /* TODO! */
}


int radix_grow(struct radix_tree *tree, unsigned long max)
{
	panic("Not implemented");
	return -1; /* TODO! */
}

int radix_preload(struct radix_tree *tree, int flags)
{
	panic("Not implemented");
	return -1; /* TODO! */
}


void *radix_tag_set(struct radix_tree *tree, unsigned long key, int tag)
{
	panic("Tagging not implemented!");
	return (void*)-1; /* TODO! */
}

void *radix_tag_clear(struct radix_tree *tree, unsigned long key, int tag)
{
	panic("Tagging not implemented!");
	return (void*)-1; /* TODO! */
}

int radix_tag_get(struct radix_tree *tree, unsigned long key, int tag)
{
	panic("Tagging not implemented!");
	return -1; /* TODO! */
}

int radix_tree_tagged(struct radix_tree *tree, int tag)
{
	panic("Tagging not implemented!");
	return -1; /* TODO! */
}

int radix_tag_gang_lookup(struct radix_tree *tree, void **results,
                          unsigned long first, unsigned int max_items, int tag)
{
	panic("Tagging not implemented!");
	return -1; /* TODO! */
}

void print_radix_tree(struct radix_tree *tree)
{
	printk("Tree %p, Depth: %d, Bound: %d\n", tree, tree->depth,
	       tree->upper_bound);

	void print_rnode(struct radix_node *r_node, int depth)
	{
		if (!r_node)
			return;
		char buf[32] = {0};
		for (int i = 0; i < depth; i++)
			buf[i] = '\t';
		printk("%sRnode %p, parent %p, myslot %p, %d items, leaf? %d\n",
		       buf, r_node, r_node->parent, r_node->my_slot, r_node->num_items,
		       r_node->leaf);
		for (int i = 0; i < NR_RNODE_SLOTS; i++) {
			if (!r_node->items[i])
				continue;
			if (r_node->leaf)
				printk("\t%sRnode Item %d: %p\n", buf, i, r_node->items[i]);
			else
				print_rnode(r_node->items[i], depth + 1);
		}
	}
	print_rnode(tree->root, 0);
}
