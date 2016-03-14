/* Copyright (c) 2015 Google Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Event bitmaps.  These are a type of event mailbox where the message type is
 * translated to a bit, which is set in the bitmap. */

#pragma once

#include <ros/evbitmap.h>

__BEGIN_DECLS

void evbitmap_init(struct evbitmap *evbm);
void evbitmap_cleanup(struct evbitmap *evbm);
bool evbitmap_is_empty(struct evbitmap *evbm);
void evbitmap_init(struct evbitmap *evbm);
bool get_evbitmap_msg(struct evbitmap *evbm, struct event_msg *ev_msg);

__END_DECLS
