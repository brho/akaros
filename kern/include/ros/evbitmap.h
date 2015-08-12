/* Copyright (c) 2015 Google Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Event bitmaps.  These are a type of event mailbox where the message type is
 * translated to a bit, which is set in the bitmap. */

/* Include this outside the ifndef, due to circular include concerns. */
#include <ros/event.h>

#ifndef ROS_INC_EVBITMAP_H
#define ROS_INC_EVBITMAP_H

struct evbitmap {
	bool						check_bits;
	uint8_t						bitmap[(MAX_NR_EVENT - 1) / 8 + 1];
};

#endif /* ROS_INC_EVBITMAP_H */
