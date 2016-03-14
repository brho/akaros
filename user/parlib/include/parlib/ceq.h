/* Copyright (c) 2015 Google Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Coalescing Event Queue: encapuslates the essence of epoll/kqueue in shared
 * memory: a dense array of sticky status bits.
 *
 * User side (consumer).
 *
 * When initializing, the nr_events is the maximum count of events you are
 * tracking, e.g. 100 FDs being tapped, but not the actual FD numbers.
 *
 * The ring_sz is a rough guess of the number of concurrent events.  It's not a
 * big deal what you pick, but it must be a power of 2.  Otherwise the kernel
 * will probably scribble over your memory.  If you pick a value that is too
 * small, then the ring may overflow, triggering an O(n) scan of the events
 * array.  You could make it == nr_events, for reasonable behavior at the
 * expense of memory. */

#pragma once

#include <ros/ceq.h>
#include <ros/event.h>

__BEGIN_DECLS

/* If you get a non-raw event queue (with mbox, initialized by event code), then
 * you'll get a CEQ with 128 events and 128 ring slots with the OR operation.
 * It's actually doable to have the user grow the CEQ, but we don't have support
 * for that yet, so just pick a size in advance.  If you're using a CEQ, you'll
 * probably want to do it yourself. */
#define CEQ_DEFAULT_SZ 128

void ceq_init(struct ceq *ceq, uint8_t op, size_t nr_events, size_t ring_sz);
bool get_ceq_msg(struct ceq *ceq, struct event_msg *msg);
bool ceq_is_empty(struct ceq *ceq);
void ceq_cleanup(struct ceq *ceq);

__END_DECLS
