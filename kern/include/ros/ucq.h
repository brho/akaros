/* Copyright (c) 2011 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Unbounded concurrent queues.  Linked buffers/arrays of elements, in page
 * size chunks.  The pages/buffers are linked together by an info struct at the
 * beginning of the page.  Producers and consumers sync on the idxes when
 * operating in a page, and page swaps are synced via the proc* for the kernel
 * and via the ucq's u_lock for the user.
 *
 * There's a bunch of details and issues discussed in the Documentation.
 *
 * This header contains the stuff that the kernel and userspace need to agree
 * on.  Each side of the implementation will have their own .c and .h files.
 * The kernel's implementation is in kern/src/ucq.c, and the user's is in
 * user/parlib/ucq.c. */

#pragma once

#include <ros/bits/event.h>
#include <ros/atomic.h>
#include <ros/arch/mmu.h>

#ifdef ROS_KERNEL
#include <arch/arch.h>
#else
#include <parlib/arch/arch.h>
#endif

/* #include <ros/event.h> included below */

/* The main UCQ structure, contains indexes and start points (for the indexes),
 * etc. */
struct ucq {
	atomic_t			prod_idx;     /* both pg and slot nr */
	atomic_t			spare_pg;     /* mmaped, unused page */
	atomic_t			nr_extra_pgs; /* nr pages mmaped */
	atomic_t			cons_idx;     /* cons pg and slot nr */
	bool				prod_overflow;/* prevent wraparound */
	bool				ucq_ready;
	/* Userspace lock for modifying the UCQ */
	uint32_t			u_lock[2];
};

/* Struct at the beginning of every page/buffer, tracking consumers and
 * pointing to the next one, so that the consumer can follow. */
struct ucq_page_header {
	uintptr_t			cons_next_pg; /* next page to consume */
	atomic_t 			nr_cons; /* like an inverted refcnt */
};

struct msg_container {
	struct event_msg		ev_msg;
	bool				ready;
};

struct ucq_page {
	struct ucq_page_header		header;
	struct msg_container		msgs[];
};

#define UCQ_WARN_THRESH		1000		/* nr pages befor warning */

#define NR_MSG_PER_PAGE ((PGSIZE - ROUNDUP(sizeof(struct ucq_page_header),     \
                                           __alignof__(struct msg_container))) \
                         / sizeof(struct msg_container))

/* A slot encodes both the page addr and the count within the page */
static bool slot_is_good(uintptr_t slot)
{
	uintptr_t counter = PGOFF(slot);
	uintptr_t pg_addr = PTE_ADDR(slot);

	return ((counter < NR_MSG_PER_PAGE) && pg_addr) ? TRUE : FALSE;
}

/* Helper: converts a slot/index into a msg container.  The ucq_page is the
 * PPN/PTE_ADDR of 'slot', and the specific slot *number* is the PGOFF.  Assumes
 * the slot is good.  If it isn't, you're going to get random memory.
 *
 * Note that this doesn't actually read the memory, just computes an address. */
static inline struct msg_container *slot2msg(uintptr_t slot)
{
	return &((struct ucq_page*)PTE_ADDR(slot))->msgs[PGOFF(slot)];
}
