/* Copyright (c) 2013 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Simple ring-buffer tracing for in-kernel events. */

#include <trace.h>
#include <smp.h>
#include <string.h>

void trace_ring_init(struct trace_ring *tr, void *buf, size_t buf_size,
                     size_t event_size)
{
	tr->tr_buf = buf;
	tr->tr_buf_sz = buf_size;
	tr->tr_event_sz_shift = LOG2_UP(event_size);
	tr->tr_max = (1 << LOG2_DOWN(buf_size / (1 << tr->tr_event_sz_shift)));
	printd("Initializing TR with %d elements of shift %d (%d)\n",
	       tr->tr_max, tr->tr_event_sz_shift, (1 << tr->tr_event_sz_shift));
	trace_ring_reset_and_clear(tr);
}

void trace_ring_reset(struct trace_ring *tr)
{
	tr->tr_next = 0;
}

void trace_ring_reset_and_clear(struct trace_ring *tr)
{
	memset(tr->tr_buf, 0, tr->tr_buf_sz);
	tr->tr_next = 0;
}

void trace_ring_foreach(struct trace_ring *tr, trace_handler_t tr_func,
                        void *data)
{
	for (int i = 0; i < tr->tr_max; i++)
		tr_func(tr->tr_buf + (i << tr->tr_event_sz_shift), data);
}
