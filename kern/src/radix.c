/* Copyright (c) 2010 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Radix Trees!  Just the basics, doesn't do tagging or anything fancy. */

#include <ros/errno.h>
#include <radix.h>
#include <slab.h>
#include <string.h>
#include <stdio.h>

struct kmem_cache *radix_kcache;
static struct radix_node *__radix_lookup_node(struct radix_tree *tree,
                                              unsigned long key,
                                              bool extend);
static void __radix_remove_slot(struct radix_node *r_node, struct radix_node **slot);

/* Initializes the radix tree system, mostly just builds the kcache */
void radix_init(void)
{
	radix_kcache = kmem_cache_create("radix_nodes", sizeof(struct radix_node),
	                                 __alignof__(struct radix_node), 0, 0, 0);
}

/* Initializes a tree dynamically */
void radix_tree_init(struct radix_tree *tree)
{
	tree->root = 0;
	tree->depth = 0;
	tree->upper_bound = 0;
}

/* Will clean up all the memory associated with a tree.  Shouldn't be necessary
 * if you delete all of the items, which you should do anyways since they are
 * usually void*.  Might expand this to have a function to call on every leaf
 * slot. */
void radix_tree_destroy(struct radix_tree *tree)
{
	/* Currently, we may have a root node, even if all the elements were removed
	 */
	panic("Not implemented");
}

/* Attempts to insert an item in the tree at the given key.  ENOMEM if we ran
 * out of memory, EEXIST if an item is already in the tree. */
int radix_insert(struct radix_tree *tree, unsigned long key, void *item)
{
	printd("RADIX: insert %08p at %d\n", item, key);
	struct radix_node *r_node;
	void **slot;
	/* Is the tree tall enough?  if not, it needs to grow a level.  This will
	 * also create the initial node (upper bound starts at 0). */
	while (key >= tree->upper_bound) {
		r_node = kmem_cache_alloc(radix_kcache, 0);
		if (!r_node)
			return -ENOMEM;
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
		tree->root = r_node;
		r_node->my_slot = &tree->root;
		tree->depth++;
		tree->upper_bound = 1 << (LOG_RNODE_SLOTS * tree->depth);
	}
	assert(tree->root);
	/* the tree now thinks it is tall enough, so find the last node, insert in
	 * it, etc */
	r_node = __radix_lookup_node(tree, key, TRUE);
	assert(r_node);		/* we want an ENOMEM actually, but i want to see this */
	slot = &r_node->items[key & (NR_RNODE_SLOTS - 1)];
	if (*slot)
		return -EEXIST;
	*slot = item;
	r_node->num_items++;
	return 0;
}

/* Removes an item from it's parent's structure, freeing the parent if there is
 * nothing left, potentially recursively. */
static void __radix_remove_slot(struct radix_node *r_node, struct radix_node **slot)
{
	assert(*slot);		/* make sure there is something there */
	*slot = 0;
	r_node->num_items--;
	/* this check excludes the root, but the if else handles it.  For now, once
	 * we have a root, we'll always keep it (will need some changing in
	 * radix_insert() */
	if (!r_node->num_items && r_node->parent) {
		if (r_node->parent)
			__radix_remove_slot(r_node->parent, r_node->my_slot);
		else			/* we're the last node, attached to the actual tree */
			*(r_node->my_slot) = 0;
		kmem_cache_free(radix_kcache, r_node);
	}
}

/* Removes a key/item from the tree, returning that item (the void*).  If it
 * detects a radix_node is now unused, it will dealloc that node.  Though the
 * tree will still think it is tall enough to handle its old upper_bound.  It
 * won't "shrink". */
void *radix_delete(struct radix_tree *tree, unsigned long key)
{
	printd("RADIX: delete %d\n", key);
	void **slot;
	void *retval;
	struct radix_node *r_node = __radix_lookup_node(tree, key, 0);
	if (!r_node)
		return 0;
	slot = &r_node->items[key & (NR_RNODE_SLOTS - 1)];
	retval = *slot;
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
	printd("RADIX: lookup %d\n", key);
	void **slot = radix_lookup_slot(tree, key);
	if (!slot)
		return 0;
	return *slot;
}

/* Returns a pointer to the radix_node holding a given key.  0 if there is no
 * such node, due to the tree being too small or something.
 *
 * If the depth is greater than one, we need to walk down the tree a level.  The
 * key is 'partitioned' among the levels of the tree, like so:
 * ......444444333333222222111111
 *
 * If an interior node of the tree is missing, this will add one if it was
 * directed to extend the tree. */
static struct radix_node *__radix_lookup_node(struct radix_tree *tree,
                                              unsigned long key, bool extend)
{
	printd("RADIX: lookup_node %d, %d\n", key, extend);
	unsigned long idx;
	struct radix_node *child_node, *r_node = tree->root;
	if (key	>= tree->upper_bound) {
		if (extend)
			warn("Bound (%d) not set for key %d!\n", tree->upper_bound, key);
		return 0;
	}
	for (int i = tree->depth; i > 1; i--) {	 /* i = ..., 4, 3, 2 */
		idx = (key >> (LOG_RNODE_SLOTS * (i - 1))) & (NR_RNODE_SLOTS - 1);
		/* There might not be a node at this part of the tree */
		if (!r_node->items[idx]) {
			if (!extend) {
				return 0;
			} else {
				/* so build one, possibly returning 0 if we couldn't */
				child_node = kmem_cache_alloc(radix_kcache, 0);
				if (!child_node)
					return 0;
				r_node->items[idx] = child_node;
				memset(child_node, 0, sizeof(struct radix_node));
				/* when we are on the last iteration (i == 2), the child will be
				 * a leaf. */
				child_node->leaf = (i == 2) ? TRUE : FALSE;
				child_node->parent = r_node;
				child_node->my_slot = (struct radix_node**)&r_node->items[idx];
				r_node->num_items++;
				r_node = (struct radix_node*)r_node->items[idx];
			}
		} else {
			r_node = (struct radix_node*)r_node->items[idx];
		}
	}
	return r_node;
}

/* Returns a pointer to the slot for the given key.  0 if there is no such slot,
 * etc */
void **radix_lookup_slot(struct radix_tree *tree, unsigned long key)
{
	printd("RADIX: lookup slot %d\n", key);
	struct radix_node *r_node = __radix_lookup_node(tree, key, FALSE);
	if (!r_node)
		return 0;
	key = key & (NR_RNODE_SLOTS - 1);
	return &r_node->items[key];
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
	printk("Tree %08p, Depth: %d, Bound: %d\n", tree, tree->depth,
	       tree->upper_bound);

	void print_rnode(struct radix_node *r_node, int depth)
	{
		if (!r_node)
			return;
		char buf[32] = {0};
		for (int i = 0; i < depth; i++)
			buf[i] = '\t';
		printk("%sRnode %08p, parent %08p, myslot %08p, %d items, leaf? %d\n",
		       buf, r_node, r_node->parent, r_node->my_slot, r_node->num_items,
		       r_node->leaf);
		for (int i = 0; i < NR_RNODE_SLOTS; i++) {
			if (!r_node->items[i])
				continue;
			if (r_node->leaf)
				printk("\t%sRnode Item %d: %08p\n", buf, i, r_node->items[i]);
			else
				print_rnode(r_node->items[i], depth + 1);
		}
	}
	print_rnode(tree->root, 0);
}
