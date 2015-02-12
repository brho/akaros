/* Copyright (c) 2013 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Simple ring-buffer tracing for in-kernel events.  The rings have a
 * power-of-two number of slots, and each entry size will be rounded up to the
 * nearest power of two.  Ring slot acquisition by default is thread-safe, but
 * we provide racy helpers if you want a little less overhead.
 *
 * Users need to provide a contiguous memory buffer and the size of an event
 * struct to init.  For example:
 *
 * 		trace_ring_init(my_trace_ring_ptr, my_buf, buf_sz, event_sz);
 *
 * And then to store a trace, first get a slot, then fill it in:
 *
 * 		struct my_trace_event *my_trace = get_trace_slot(my_trace_ring_ptr);
 * 		if (my_trace)	// only need to check if we aren't overwriting
 * 			my_trace = whatever;
 *
 * Later, to process the traces, provide a function pointer to
 * trace_ring_foreach().  This performs the func on all traces in the ring,
 * including the unused:
 *
 * 		void trace_handler(void *trace_event, void *data)
 * 		{
 * 			whatever();
 * 		}
 * 		trace_ring_foreach(my_trace_ring_ptr, trace_handler, optional_blob);
 *
 * Rings can be smp safe, per cpu or racy, and can overwrite entries or not.
 * If you are not overwriting, the ring will stop giving you slots.  You need
 * to reset the ring to get fresh slots again.  If you are overwriting, you
 * don't need to check the return value of get_trace_slot_overwrite().  Per cpu
 * rings are interrupt safe.
 *
 * Given there is overwrite, tr_next doesn't really tell us which ones were
 * used.  So your handler should check for a flag or something.  Timestamps
 * might help make sense of the data in these cases too. */

#ifndef ROS_INC_TRACE_H
#define ROS_INC_TRACE_H

#include <ros/common.h>
#include <arch/arch.h>
#include <assert.h>

struct trace_ring {
	uint8_t						*tr_buf;
	size_t						tr_buf_sz;
	unsigned int				tr_event_sz_shift;
	unsigned int				tr_max;
	unsigned long				tr_next;
};

typedef void (*trace_handler_t)(void *event, void *blob);

static inline void *get_trace_slot(struct trace_ring *tr);
static inline void *get_trace_slot_overwrite(struct trace_ring *tr);
static inline void *get_trace_slot_racy(struct trace_ring *tr);
static inline void *get_trace_slot_overwrite_racy(struct trace_ring *tr);

void trace_ring_init(struct trace_ring *tr, void *buf, size_t buf_size,
                     size_t event_size);
void trace_ring_reset(struct trace_ring *tr);
void trace_ring_reset_and_clear(struct trace_ring *tr);
void trace_ring_foreach(struct trace_ring *tr, trace_handler_t tr_func,
                        void *data);

/* Inlined funcs, declared above */

/* Helper */
/* Get next trace ring slot with no wrapping */
static inline void *__get_tr_slot(struct trace_ring *tr, unsigned long ind)
{
	dassert(0 <= ind && ind < tr->tr_max);
	/* event sizes are rounded up to the nearest power of 2 (sz_shift) */
	return (void *) (tr->tr_buf + (ind << tr->tr_event_sz_shift));
}

/* Get next trace ring slot with wrapping */
static inline void *
__get_tr_slot_overwrite(struct trace_ring *tr, unsigned long slot)
{
	/* tr_max is a power of 2, we're ignoring the upper bits of slot */
	slot &= tr->tr_max - 1;
	return __get_tr_slot(tr, slot);
}

/* Interrupt safe modification of tr_next */
static inline unsigned int
__get_next_percpu_and_add(struct trace_ring *tr, int8_t n)
{
	int8_t irq_state = 0;
	disable_irqsave(&irq_state);
	unsigned int ret = tr->tr_next;
	tr->tr_next += n;
	enable_irqsave(&irq_state);
	return ret;
}

static inline void *get_trace_slot(struct trace_ring *tr)
{
	/* Using syncs, instead of atomics, since we access tr_next as both atomic
	 * and 'normal'. */
	unsigned long my_slot = __sync_fetch_and_add(&tr->tr_next, 1);
	/* We can briefly go over, so long as we subtract back down to where we were
	 * before.  This will work so long as we don't have ~2^64 threads... */
	if (my_slot >= tr->tr_max) {
		__sync_fetch_and_add(&tr->tr_next, -1);
		return 0;
	}
	return __get_tr_slot(tr, my_slot);
}

static inline void *get_trace_slot_overwrite(struct trace_ring *tr)
{
	return __get_tr_slot_overwrite(tr, __sync_fetch_and_add(&tr->tr_next, 1));
}

static inline void *get_trace_slot_percpu(struct trace_ring *tr)
{
	unsigned long my_slot = __get_next_percpu_and_add(tr, 1);
	if (my_slot >= tr->tr_max) {
		/* See comment in get_trace_slot. */
	    __get_next_percpu_and_add(tr, -1);
		return 0;
	}
	return __get_tr_slot(tr, my_slot);
}

static inline void *get_trace_slot_overwrite_percpu(struct trace_ring *tr)
{
	return __get_tr_slot_overwrite(tr, __get_next_percpu_and_add(tr, 1));
}

static inline void *get_trace_slot_racy(struct trace_ring *tr)
{
	unsigned long my_slot = tr->tr_next;
	if (my_slot >= tr->tr_max)
		return 0;
	tr->tr_next++;
	return __get_tr_slot(tr, my_slot);
}

static inline void *get_trace_slot_overwrite_racy(struct trace_ring *tr)
{
	return __get_tr_slot_overwrite(tr, tr->tr_next++);
}

#endif /* ROS_INC_TRACE_H */
