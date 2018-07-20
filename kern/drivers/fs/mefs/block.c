/* Copyright (c) 2016, 2018 Google Inc
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Memory Extent Filesystem block allocation
 *
 * We use a power-of-two list allocator, similar to the arena allocator.
 * There's no xalloc, importing, qcaches, or anything like that.  The superblock
 * is analogous to the base arena: it must be self-sufficient.
 *
 * All of the structures are "on disk."  In theory, that should change as often
 * as a filesystem's disk structures change, which is rarely if ever.  Once it
 * is done.  =)  All values are in host-endian, and we operate directly on RAM.
 *
 * There's no protection for metadata corruption - if you crash in the middle of
 * a tree-changing operation, you're out of luck.
 *
 * Unlike the arena allocator, we don't return a "void *", we actually return
 * a pointer to the btag.  All of our users (the rest of mefs) will put up with
 * this layer of indirection.  In exchange, we don't have to muck around with
 * hash tables to find the btag when the segment is freed.
 *
 * This all assumes the caller manages synchronization (e.g. locks).
 *
 * This uses the BSD list macros, which technically is not guaranteed to not
 * change.  If someone wants to replace them with local versions that are bound
 * to the filesystem's disk format, then be my guest.  This doesn't use the
 * rbtree code, though we probably could with the same justification for using
 * the BSD list code.  But it'd be a bit more of a pain to roll our own for
 * that, and I doubt it is necessary.
 */

#include "mefs.h"
#include <err.h>

static struct mefs_btag *__get_from_freelists(struct mefs_superblock *sb,
                                              int list_idx);
static bool __account_alloc(struct mefs_superblock *sb, struct mefs_btag *bt,
                            size_t size, struct mefs_btag *new);

/* Bootstrap from the free list */
static void __ensure_some_btags(struct mefs_superblock *sb)
{
	struct mefs_btag *bt, *tags;
	size_t nr_bts = MEFS_QUANTUM / sizeof(struct mefs_btag);

	if (!BSD_LIST_EMPTY(&sb->unused_btags))
		return;
	bt = __get_from_freelists(sb, LOG2_UP(MEFS_QUANTUM));
	if (!bt)
		error(ENOMEM, "Unable to get more BTs in mefs!");
	tags = (struct mefs_btag*)bt->start;
	if (__account_alloc(sb, bt, MEFS_QUANTUM, &tags[0])) {
		/* We used the tag[0]; we'll have to skip over it now. */
		tags++;
		nr_bts--;
	}
	for (int i = 0; i < nr_bts; i++)
		BSD_LIST_INSERT_HEAD(&sb->unused_btags, &tags[i], misc_link);
}

static struct mefs_btag *__get_btag(struct mefs_superblock *sb)
{
	struct mefs_btag *bt;

	bt = BSD_LIST_FIRST(&sb->unused_btags);
	assert(bt);
	BSD_LIST_REMOVE(bt, misc_link);
	return bt;
}

static void __free_btag(struct mefs_superblock *sb, struct mefs_btag *bt)
{
	BSD_LIST_INSERT_HEAD(&sb->unused_btags, bt, misc_link);
}

static void __track_free_seg(struct mefs_superblock *sb, struct mefs_btag *bt)
{
	int list_idx = LOG2_DOWN(bt->size);

	bt->status = MEFS_BTAG_FREE;
	BSD_LIST_INSERT_HEAD(&sb->free_segs[list_idx], bt, misc_link);
}

static void __untrack_free_seg(struct mefs_superblock *sb, struct mefs_btag *bt)
{
	BSD_LIST_REMOVE(bt, misc_link);
}

/* This is a little slow, and is a consequence of not using a tree.  However,
 * the common case caller was when @bt was created from an old one, and is
 * likely to be right after it.  The old one is the @hint, which is where to
 * start our search. */
static void __insert_btag(struct mefs_btag_list *list, struct mefs_btag *bt,
                          struct mefs_btag *hint)
{
	struct mefs_btag *i, *last = NULL;
	bool hinted = false;

	BSD_LIST_FOREACH(i, list, all_link) {
		if (!hinted && hint) {
			i = hint;
			hinted = true;
		}
		if (bt->start < i->start) {
			BSD_LIST_INSERT_BEFORE(i, bt, all_link);
			return;
		}
		if (bt->start == i->start)
			panic("BT %p == i %p in list %p!", bt, i, list);
		last = i;
	}
	if (last)
		BSD_LIST_INSERT_AFTER(last, bt, all_link);
	else
		BSD_LIST_INSERT_HEAD(list, bt, all_link);
}

/* Unlink the arena allocator, we don't track the segments on an allocated list.
 * Our caller is the one that keeps track of it. */
static void __track_alloc_seg(struct mefs_superblock *sb, struct mefs_btag *bt)
{
	bt->status = MEFS_BTAG_ALLOC;
}

/* Helper: we decided we want to alloc part of @bt, which has been removed from
 * its old list.  We need @size units.  The rest can go back.
 *
 * Takes @new, which we'll use if we need a new btag.  If @new is NULL, we'll
 * allocate one.  If we used the caller's btag, we'll return TRUE. */
static bool __account_alloc(struct mefs_superblock *sb, struct mefs_btag *bt,
                            size_t size, struct mefs_btag *new)
{
	bool ret = FALSE;

	assert(bt->status == MEFS_BTAG_FREE);
	if (bt->size != size) {
		assert(bt->size > size);
		if (new)
			ret = TRUE;
		else
			new = __get_btag(sb);
		new->start = bt->start + size;
		new->size = bt->size - size;
		bt->size = size;
		__track_free_seg(sb, new);
		__insert_btag(&sb->all_segs, new, bt);
	}
	__track_alloc_seg(sb, bt);
	sb->amt_alloc_segs += size;
	return ret;
}

static struct mefs_btag *__get_from_freelists(struct mefs_superblock *sb,
                                              int list_idx)
{
	struct mefs_btag *ret = NULL;

	for (int i = list_idx; i < MEFS_NR_FREE_LISTS; i++) {
		ret = BSD_LIST_FIRST(&sb->free_segs[i]);
		if (ret) {
			BSD_LIST_REMOVE(ret, misc_link);
			break;
		}
	}
	return ret;
}

/* This uses the arena's "best fit" policy.  You could imagine building a
 * version that cares about alignment too, for e.g. huge pages. */
struct mefs_btag *mefs_ext_alloc(struct mefs_superblock *sb, size_t size)
{
	int list_idx = LOG2_DOWN(size);
	struct mefs_btag *bt_i, *best = NULL;

	if (!size)
		error(EINVAL, "mefs_ext_alloc with 0 size!");
	__ensure_some_btags(sb);
	size = ROUNDUP(size, MEFS_QUANTUM);
	BSD_LIST_FOREACH(bt_i, &sb->free_segs[list_idx], misc_link) {
		if (bt_i->size >= size) {
			if (!best || (best->size > bt_i->size))
				best = bt_i;
		}
	}
	if (best)
		BSD_LIST_REMOVE(best, misc_link);
	else
		best = __get_from_freelists(sb, list_idx + 1);
	if (!best)
		error(ENOMEM, "couldn't find segment in mefs!");
	__account_alloc(sb, best, size, NULL);
	return best;
}

static bool __merge_right_to_left(struct mefs_superblock *sb,
                                  struct mefs_btag *left,
                                  struct mefs_btag *right)
{
	if (left->status != MEFS_BTAG_FREE)
		return false;
	if (right->status != MEFS_BTAG_FREE)
		return false;
	if (left->start + left->size == right->start) {
		__untrack_free_seg(sb, left);
		__untrack_free_seg(sb, right);
		left->size += right->size;
		__track_free_seg(sb, left);
		BSD_LIST_REMOVE(right, all_link);
		__free_btag(sb, right);
		return true;
	}
	return false;
}

static void __coalesce_free_seg(struct mefs_superblock *sb,
                                struct mefs_btag *bt)
{
	struct mefs_btag *bt_p, *bt_n;

	bt_n = BSD_LIST_NEXT(bt, all_link);
	if (bt_n)
		__merge_right_to_left(sb, bt, bt_n);
	bt_p = BSD_LIST_PREV(bt, &sb->all_segs, mefs_btag, all_link);
	if (bt_p)
		__merge_right_to_left(sb, bt_p, bt);
}

void mefs_ext_free(struct mefs_superblock *sb, struct mefs_btag *bt)
{
	void *to_free_addr = 0;
	size_t to_free_sz = 0;

	sb->amt_alloc_segs -= bt->size;
	__track_free_seg(sb, bt);
	/* Don't use bt after this: */
	__coalesce_free_seg(sb, bt);
	sb->amt_total_segs -= to_free_sz;
}

/* Bump allocates space in the segment [seg_alloc, seg_alloc + seg_amt).
 * Returns the allocation address and updates the allocator's values by
 * reference.  Throws on error. */
static void *bump_zalloc(size_t amt, size_t align, uintptr_t *seg_alloc,
                         size_t *seg_amt)
{
	size_t align_diff;
	void *ret;

	align_diff = ALIGN(*seg_alloc, align) - *seg_alloc;
	if (*seg_amt < amt + align_diff)
		error(ENOMEM, "Out of space in mefs SB");
	*seg_amt -= align_diff;
	*seg_alloc += align_diff;
	ret = (void*)*seg_alloc;
	*seg_alloc += amt;
	*seg_amt -= amt;
	memset(ret, 0, amt);
	return ret;
}

/* Creates a superblock and adds the memory segment.  The SB will be at the
 * beginning of the segment. */
struct mefs_superblock *mefs_super_create(uintptr_t init_seg, size_t size)
{
	struct mefs_superblock *sb;
	struct mefs_btag *bt;
	uintptr_t seg_alloc = init_seg;
	size_t seg_amt = size;

	sb = bump_zalloc(sizeof(*sb), __alignof__(*sb), &seg_alloc, &seg_amt);
	memcpy(sb->magic, MEFS_MAGIC, sizeof(sb->magic));
	BSD_LIST_INIT(&sb->all_segs);
	BSD_LIST_INIT(&sb->unused_btags);
	for (int i = 0; i < MEFS_NR_FREE_LISTS; i++)
		BSD_LIST_INIT(&sb->free_segs[i]);

	bt = bump_zalloc(sizeof(*bt), __alignof__(*bt), &seg_alloc, &seg_amt);
	BSD_LIST_INSERT_HEAD(&sb->unused_btags, bt, misc_link);
	
	seg_alloc = ALIGN(seg_alloc, MEFS_QUANTUM);
	seg_amt = ALIGN_DOWN(seg_amt, MEFS_QUANTUM);

	mefs_super_add(sb, seg_alloc, seg_amt);

	return sb;
}

/* Ignoring size for now.  Could use it for sanity checks. */
struct mefs_superblock *mefs_super_attach(uintptr_t init_seg, size_t size)
{
	struct mefs_superblock *sb;

	init_seg = ALIGN(init_seg, sizeof(*sb));
	sb = (struct mefs_superblock*)init_seg;
	if (strcmp(sb->magic, MEFS_MAGIC))
		return NULL;
	return sb;
}

void mefs_super_add(struct mefs_superblock *sb, uintptr_t seg, size_t size)
{
	struct mefs_btag *bt;

	__ensure_some_btags(sb);
	bt = __get_btag(sb);
	bt->start = seg;
	bt->size = size;
	sb->amt_total_segs += size;
	__track_free_seg(sb, bt);
	__insert_btag(&sb->all_segs, bt, NULL);
}

void mefs_super_destroy(struct mefs_superblock *sb)
{
	memset(sb->magic, 0xa, sizeof(sb->magic));
}

void mefs_super_dump(struct mefs_superblock *sb)
{
	struct mefs_btag *i;

	printk("All segs\n");
	BSD_LIST_FOREACH(i, &sb->all_segs, all_link)
		printk("bt %p, start %p, +%lu, %s\n", i, i->start, i->size,
		       i->status == MEFS_BTAG_FREE ? "free" : "alloc");
}

#include <time.h>

static inline void kb_wait(void)
{
    int i;

    for (i = 0; i < 0x10000; i++) {
        if ((inb(0x64) & 0x02) == 0)
            break;
        udelay(2);
    }
}

void food()
{
	outb(0xcf9, 0x6);
	printk("ACPI cf9 FAILED\n");
}

void foot()
{
            for (int i = 0; i < 10; i++) {
                kb_wait();
                udelay(50);
                outb(0x64, 0xfe); /* Pulse reset low */
                udelay(50);
            }

	printk("KBD FAILED\n");
}
