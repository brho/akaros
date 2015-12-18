/* Copyright (c) 2015 Google Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * close() callbacks.  See sys/close_cb.h. */

#include <sys/close_cb.h>
#include <ros/common.h>

struct close_cb *close_callbacks;

void register_close_cb(struct close_cb *cb)
{
	struct close_cb *old;

	do {
		old = ACCESS_ONCE(close_callbacks);
		cb->next = old;
	} while (!__sync_bool_compare_and_swap(&close_callbacks, old, cb));
}
