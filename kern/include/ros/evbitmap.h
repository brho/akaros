/* Copyright (c) 2015 Google Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Event bitmaps.  These are a type of event mailbox where the message type is
 * translated to a bit, which is set in the bitmap. */

#pragma once

#include <ros/bits/event.h>

struct evbitmap {
	bool				check_bits;
	uint8_t				bitmap[(MAX_NR_EVENT - 1) / 8 + 1];
};
