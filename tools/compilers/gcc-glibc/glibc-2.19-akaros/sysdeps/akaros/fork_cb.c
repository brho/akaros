/* Copyright (c) 2016 Google Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * fork() callbacks.  See sys/fork_cb.h. */

#include <sys/fork_cb.h>
#include <ros/common.h>

struct fork_cb *fork_callbacks;

void register_fork_cb(struct fork_cb *cb)
{
	struct fork_cb *old;

	do {
		old = ACCESS_ONCE(fork_callbacks);
		cb->next = old;
	} while (!__sync_bool_compare_and_swap(&fork_callbacks, old, cb));
}
