/* Copyright (c) 2011 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Userspace utility functions for receiving events and notifications (IPIs).
 * Some are higher level than others; just use what you need. */ 

#include <ros/event.h>
#include <ros/procdata.h>
#include <ros/bcq.h>
#include <arch/bitmask.h>
#include <vcore.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <parlib.h>
#include <event.h>

/********* Event_q Setup / Registration  ***********/

/* Get event_qs via these interfaces, since eventually we'll want to either
 * allocate from pinned memory or use some form of a slab allocator.  Also, this
 * stitches up the big_q so its ev_mbox points to its internal mbox.  Never
 * access the internal mbox directly. */
struct event_queue *get_big_event_q(void)
{
	/* TODO: (PIN) should be pinned memory */
	struct event_queue_big *big_q = malloc(sizeof(struct event_queue_big));
	memset(big_q, 0, sizeof(struct event_queue_big));
	big_q->ev_mbox = &big_q->ev_imbox;
	return (struct event_queue*)big_q;
}

/* Give it up */
void put_big_event_q(struct event_queue *ev_q)
{
	/* if we use something other than malloc, we'll need to be aware that ev_q
	 * is actually an event_queue_big.  One option is to use the flags, though
	 * this could be error prone. */
	free(ev_q);
}

/* Need to point this event_q to an mbox - usually to a vcpd */
struct event_queue *get_event_q(void)
{
	/* TODO: (PIN) should be pinned memory */
	struct event_queue *ev_q = malloc(sizeof(struct event_queue));
	memset(ev_q, 0, sizeof(struct event_queue));
	return ev_q;
}

/* Gets a small ev_q, with ev_mbox pointing to the vcpd mbox of vcoreid */
struct event_queue *get_event_q_vcpd(uint32_t vcoreid)
{
	struct event_queue *ev_q = get_event_q();
	ev_q->ev_mbox = &__procdata.vcore_preempt_data[vcoreid].ev_mbox;
	return ev_q;
}

void put_event_q(struct event_queue *ev_q)
{
	/* if we use something other than malloc, we'll need to be aware that ev_q
	 * is actually an event_queue_big. */
	free(ev_q);
}

/* Sets ev_q to be the receiving end for kernel event ev_type */
void register_kevent_q(struct event_queue *ev_q, unsigned int ev_type)
{
	__procdata.kernel_evts[ev_type] = ev_q;
}

/* Clears the event, returning an ev_q if there was one there.  You'll need to
 * free it. */
struct event_queue *clear_kevent_q(unsigned int ev_type)
{
	struct event_queue *ev_q = __procdata.kernel_evts[ev_type];
	__procdata.kernel_evts[ev_type] = 0;
	return ev_q;
}

/* Enables an IPI/event combo for ev_type sent to vcoreid's default mbox.  IPI
 * if you want one or not.  This is the simplest thing applications may want,
 * and shows how you can put the other event functions together to get similar
 * things done. */
void enable_kevent(unsigned int ev_type, uint32_t vcoreid, bool ipi)
{
	struct event_queue *ev_q = get_event_q_vcpd(vcoreid);
	ev_q->ev_flags = ipi ? EVENT_IPI : 0;
	ev_q->ev_vcore = vcoreid;
	ev_q->ev_handler = 0;
	register_kevent_q(ev_q, ev_type);
}

/* Stop receiving the events (one could be on the way) */
void disable_kevent(unsigned int ev_type)
{
	struct event_queue *ev_q = clear_kevent_q(ev_type);
	if (ev_q)
		put_event_q(ev_q);
	else
		printf("Tried to disable but no event_q loaded on ev_type %d", ev_type);
}

/********* Event Handling / Reception ***********/
/* Tests the ev_q to see if anything has happened on it.  Up to the caller to do
 * something with the info, such as try and dequeue or handle an overflow.
 * Flags is for the ev_q's flags (if you know it), which is to check the NO_MSG
 * style ev_qs. */
bool event_activity(struct event_mbox *ev_mbox, int flags)
{
	if (!bcq_empty(&ev_mbox->ev_msgs))
		return TRUE;
	/* Only need to check the bitmask for activity if we've had overflows or if
	 * we are a NO_MSG.  This means the client can clear its overflows. */
	if (ev_mbox->ev_overflows || (flags & EVENT_NOMSG)) {
		if (!BITMASK_IS_CLEAR(&ev_mbox->ev_bitmap, MAX_NR_EVENT))
			return TRUE;
	}
	return FALSE;
}

/* Clears the overflows, returning the number of overflows cleared. */
unsigned int event_clear_overflows(struct event_queue *ev_q)
{
	unsigned int retval = ev_q->ev_mbox->ev_overflows;
	ev_q->ev_mbox->ev_overflows = 0;
	return retval;
}

/* Somewhat ghetto helper, for the lazy.  If all you care about is an event
 * number, this will see if the event happened or not.  It will try for a
 * message, but if there is none, it will go for a bit.  Note that multiple
 * messages that overflowed could turn into just one bit. */
unsigned int get_event_type(struct event_mbox *ev_mbox)
{
	struct event_msg local_msg = {0};
	if (bcq_dequeue(&ev_mbox->ev_msgs, &local_msg, NR_BCQ_EVENTS)) {
		return local_msg.ev_type;
	}
	if (BITMASK_IS_CLEAR(&ev_mbox->ev_bitmap, MAX_NR_EVENT))
		return EV_NONE;	/* aka, 0 */
	for (int i = 0; i < MAX_NR_EVENT; i++) {
		if (GET_BITMASK_BIT(ev_mbox->ev_bitmap, i)) {
			CLR_BITMASK_BIT_ATOMIC(ev_mbox->ev_bitmap, i);
			return i;
		}
	}
	return EV_NONE;
}
