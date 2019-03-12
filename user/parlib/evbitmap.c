/* Copyright (c) 2015 Google Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Event bitmaps.  These are a type of event mailbox where the message type is
 * translated to a bit, which is set in the bitmap. */

#include <parlib/evbitmap.h>
#include <parlib/bitmask.h>
#include <string.h>

void evbitmap_init(struct evbitmap *evbm)
{
	memset(evbm, 0, sizeof(struct evbitmap));
}

void evbitmap_cleanup(struct evbitmap *evbm)
{
}

bool evbitmap_is_empty(struct evbitmap *evbm)
{
	return !evbm->check_bits;
}

bool get_evbitmap_msg(struct evbitmap *evbm, struct event_msg *ev_msg)
{
	if (evbitmap_is_empty(evbm))
		return FALSE;
	while (1) {
		for (int i = 0; i < MAX_NR_EVENT; i++) {
			if (GET_BITMASK_BIT(evbm->bitmap, i)) {
				CLR_BITMASK_BIT_ATOMIC(evbm->bitmap, i);
				/* bit messages are empty except for the type */
				memset(ev_msg, 0, sizeof(struct event_msg));
				ev_msg->ev_type = i;
				return TRUE;
			}
		}
		/* If we made it here, then the bitmap might be empty. */
		evbm->check_bits = FALSE;
		/* check_bits written before we check for it being clear */
		wrmb();
		if (BITMASK_IS_CLEAR(evbm->bitmap, MAX_NR_EVENT))
			return FALSE;
		cmb();
		evbm->check_bits = TRUE;
	}
}
