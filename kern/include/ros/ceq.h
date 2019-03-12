/* Copyright (c) 2015 Google Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Coalescing Event Queue: encapuslates the essence of epoll/kqueue in shared
 * memory: a dense array of sticky status bits.
 *
 * Like all event mailboxes, CEQs are multi-producer, multi-consumer, with the
 * producer not trusting the consumer.
 *
 * The producer sends ceq_events with a certain ID (the ev_type).  Events of a
 * particular ID are coalesced in one spot, such that N events can occur and the
 * consumer only receives them as one event.  These are the "sticky status
 * bits".  For example, setting flags in an int coalesce (you can set the same
 * flag repeatedly, and it's still set), and an atomic counter coalesces small
 * increments into a larger value.  The nature of the operation (atomic_or,
 * atomic_add) depends on a field in the CEQ.  There is also a data blob field,
 * last-write-wins.  There aren't a lot of guarantees associated with it, but
 * it's still useful for some apps.
 *
 * Internally, a CEQ maintains an array of ceq_event structs for every possible
 * ID, so the maximum ID should be known at ceq creation time.  These structs
 * coalesce the events.  To keep the consumer from needing to scan the array for
 * activity, there is a separate ring buffer that contains the indices of
 * ceq_events with activity.  This is the "dense array."
 *
 * The ring buffer is actually an optimization.  If anything goes wrong, we can
 * tell the consumer to scan the entire array.  Likewise, spurious entries in
 * the ring are safe; the consumer just does an extra check.
 *
 * In general, every time we have an event, we make sure there's a pointer in
 * the ring.  That's the purposed of 'idx_posted' - whether or not we think our
 * index is posted in the ring. */

#pragma once

#include <ros/bits/event.h>
#include <ros/atomic.h>
#include <ros/ring_buffer.h>

#define CEQ_OR			1
#define CEQ_ADD			2

struct ceq_event {
	atomic_t			coalesce;	/* ev_arg2 */
	uint64_t			blob_data;	/* ev_arg3 */
	bool				idx_posted;	/* syncing with cons */
	uint64_t			user_data;
};

/* The events array and the ring buffer are provided by the consumer.
 *
 * Ring values are -1 for "unconsumed" and an index into *events otherwise.
 *
 * Similar to BCQs, the ring buffer must be a power of two and is managed with
 * three index variables:
 *
 * prod_idx:     the next slot to be produced
 * cons_pvt_idx: the next slot a consumer can claim
 * cons_pub_idx: the last slot (farthest left / oldest) that hasn't been
 *               consumed/made ready to be produced by the producer (it is
 *               what the consumer produces).
 *
 * The ring is has no new entries that need consumed when prod == pvt.   The
 * number of entries filled is prod - pub.  The number of available entries
 * (nr_empty) is the size - (prod - pub). */

struct ceq {
	struct ceq_event		*events;	/* consumer pointer */
	unsigned int			nr_events;
	unsigned int			last_recovered;
	atomic_t			max_event_ever;
	int32_t				*ring;		/* consumer pointer */
	uint32_t			ring_sz;	/* size (power of 2) */
	uint8_t				operation;	/* e.g. CEQ_OR */
	bool				ring_overflowed;
	bool				overflow_recovery;
	atomic_t			prod_idx;	/* next slot to fill */
	atomic_t			cons_pub_idx;	/* consumed so far */
	atomic_t			cons_pvt_idx;	/* next slot to get */
	uint32_t			u_lock[2];	/* user space lock */
};
