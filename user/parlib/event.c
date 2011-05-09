/* Copyright (c) 2011 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Userspace utility functions for receiving events and notifications (IPIs).
 * Some are higher level than others; just use what you need. */ 

#include <ros/event.h>
#include <ros/procdata.h>
#include <ros/bcq.h>
#include <bitmask.h>
#include <vcore.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <parlib.h>
#include <event.h>
#include <uthread.h>

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
void enable_kevent(unsigned int ev_type, uint32_t vcoreid, int ev_flags)
{
	struct event_queue *ev_q = get_event_q_vcpd(vcoreid);
	ev_q->ev_flags = ev_flags;
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
	/* BCQ returns 0 on success, so this will dequeue and return the type. */
	if (!bcq_dequeue(&ev_mbox->ev_msgs, &local_msg, NR_BCQ_EVENTS)) {
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

/* Actual Event Handling */

/* List of handlers, process-wide, that the 2LS should fill in.  They all must
 * return (don't context switch to a u_thread), and need to handle ev_msg being
 * 0. */
handle_event_t ev_handlers[MAX_NR_EVENT] = {[EV_EVENT] handle_ev_ev, 0};

/* Handle an mbox.  This is the receive-side processing of an event_queue.  It
 * takes an ev_mbox, since the vcpd mbox isn't a regular ev_q.  For now, we
 * check for preemptions between each event handler. */
static int handle_mbox(struct event_mbox *ev_mbox, unsigned int flags)
{
	struct event_msg local_msg;
	unsigned int ev_type;
	bool overflow = FALSE;
	int retval = 0;
	uint32_t vcoreid = vcore_id();

	if (!event_activity(ev_mbox, flags))
		return retval;
	/* Try to dequeue, dispatch whatever you get.  TODO consider checking for
	 * overflow first */
	while (!bcq_dequeue(&ev_mbox->ev_msgs, &local_msg, NR_BCQ_EVENTS)) {
		ev_type = local_msg.ev_type;
		printd("BCQ: ev_type: %d\n", ev_type);
		if (ev_handlers[ev_type])
			ev_handlers[ev_type](&local_msg, ev_type, overflow);
		check_preempt_pending(vcoreid);
		retval++;
	}
	/* Race here with another core clearing overflows/bits.  Don't have more
	 * than one vcore work on an mbox without being more careful of overflows
	 * (as in, assume any overflow means all bits must be checked, since someone
	 * might have not told a handler of an overflow). */
	if (ev_mbox->ev_overflows) {
		ev_mbox->ev_overflows = 0;
		overflow = TRUE;
	}
	/* Process all bits.  As far as I've seen, using overflow like this is
	 * thread safe (tested on some code in mhello, asm looks like it knows to
	 * have the function use addresses relative to the frame pointer). */
	void bit_handler(unsigned int bit) {
		printd("Bit: ev_type: %d\n", bit);
		if (ev_handlers[bit])
			ev_handlers[bit](0, bit, overflow);
		retval++;
		check_preempt_pending(vcoreid);
		/* Consider checking the queue for incoming messages while we're here */
	}
	BITMASK_FOREACH_SET(ev_mbox->ev_bitmap, MAX_NR_EVENT, bit_handler, TRUE);
	return retval;
}

/* The EV_EVENT handler - extract the ev_q from the message.  If you want this
 * to catch overflows, you'll need to register your event_queues (TODO).  Might
 * be issues with per-core handling (register globally, or just per vcore). */
void handle_ev_ev(struct event_msg *ev_msg, unsigned int ev_type, bool overflow)
{
	struct event_queue *ev_q;
	/* TODO: handle overflow (register, etc) */
	if (overflow)
		printf("Ignoring overflow!  Deal with me!\n");
	if (!ev_msg)
		return;
	ev_q = ev_msg->ev_arg3;
	if (ev_q)
		handle_event_q(ev_q);
}

/* 2LS will probably call this in vcore_entry and places where it wants to check
 * for / handle events.  This will process all the events for the given vcore.
 * Note, it probably should be the calling vcore you do this to...  Returns the
 * number of events handled. */
int handle_events(uint32_t vcoreid)
{
	struct preempt_data *vcpd = &__procdata.vcore_preempt_data[vcoreid];
	/* TODO: EVENT_NOMSG checks could be painful.  we could either keep track of
	 * whether or not the 2LS has a NOMSG ev_q pointing to its vcpd, or have the
	 * kernel set another flag for "bits" */
	return handle_mbox(&vcpd->ev_mbox, EVENT_NOMSG);
}

/* Handles the events on ev_q IAW the event_handlers[].  If the ev_q is
 * application specific, then this will dispatch/handle based on its flags. */
void handle_event_q(struct event_queue *ev_q)
{
	/* If the program wants to handle the ev_q on its own: */
	if (ev_q->ev_flags & (EVENT_JUSTHANDLEIT | EVENT_THREAD)) {
		if (!ev_q->ev_handler) {
			printf("No ev_handler installed for ev_q %08p, aborting!\n", ev_q);
			return;
		}
		if (ev_q->ev_flags & EVENT_JUSTHANDLEIT) {
			/* Remember this can't block or page fault */
			ev_q->ev_handler(ev_q);
		} else if (ev_q->ev_flags & EVENT_THREAD) {
			/* 2LS sched op.  The 2LS can use an existing thread if it wants,
			 * but do so inside spawn_thread() */
			if (sched_ops->spawn_thread)
				sched_ops->spawn_thread((uintptr_t)ev_q->ev_handler, ev_q);
			else
				printf("2LS can't spawn a thread for ev_q %08p\n", ev_q);
		}
		return;
	}
	handle_mbox(ev_q->ev_mbox, ev_q->ev_flags);
}
