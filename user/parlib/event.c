/* Copyright (c) 2011 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Userspace utility functions for receiving events and notifications (IPIs).
 * Some are higher level than others; just use what you need. */ 

#include <ros/event.h>
#include <ros/procdata.h>
#include <ucq.h>
#include <bitmask.h>
#include <vcore.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <parlib.h>
#include <event.h>
#include <uthread.h>

/* For remote VCPD mbox event handling */
__thread bool __vc_handle_an_mbox = FALSE;
__thread uint32_t __vc_rem_vcoreid;

/********* Event_q Setup / Registration  ***********/

/* Get event_qs via these interfaces, since eventually we'll want to either
 * allocate from pinned memory or use some form of a slab allocator.  Also,
 * these stitch up the big_q so its ev_mbox points to its internal mbox.  Never
 * access the internal mbox directly.
 *
 * Raw ones need to have their UCQs initialized.  If you're making a lot of
 * these, you can do one big mmap and init the ucqs on your own, which ought to
 * perform better.
 *
 * Use the 'regular' one for big_qs if you don't want to worry about the ucq
 * initalization */
struct event_queue *get_big_event_q_raw(void)
{
	/* TODO: (PIN) should be pinned memory */
	struct event_queue_big *big_q = malloc(sizeof(struct event_queue_big));
	memset(big_q, 0, sizeof(struct event_queue_big));
	big_q->ev_mbox = &big_q->ev_imbox;
	return (struct event_queue*)big_q;
}

struct event_queue *get_big_event_q(void)
{
	struct event_queue *big_q = get_big_event_q_raw();
	/* uses the simpler, internally mmapping ucq_init() */
	ucq_init(&big_q->ev_mbox->ev_msgs);
	return big_q;
}

/* Give it up.  I don't recommend calling these unless you're sure the queues
 * aren't in use (unregistered, etc). (TODO: consider some checks for this) */
void put_big_event_q_raw(struct event_queue *ev_q)
{
	/* if we use something other than malloc, we'll need to be aware that ev_q
	 * is actually an event_queue_big.  One option is to use the flags, though
	 * this could be error prone. */
	free(ev_q);
}

void put_big_event_q(struct event_queue *ev_q)
{
	ucq_free_pgs(&ev_q->ev_mbox->ev_msgs);
	put_big_event_q_raw(ev_q);
}

/* Need to point this event_q to an mbox - usually to a vcpd */
struct event_queue *get_event_q(void)
{
	/* TODO: (PIN) should be pinned memory */
	struct event_queue *ev_q = malloc(sizeof(struct event_queue));
	memset(ev_q, 0, sizeof(struct event_queue));
	return ev_q;
}

/* Gets a small ev_q, with ev_mbox pointing to the vcpd mbox of vcoreid.  If
 * ev_flags has EVENT_VCORE_PRIVATE set, it'll give you the private mbox.  o/w,
 * you'll get the public one. */
struct event_queue *get_event_q_vcpd(uint32_t vcoreid, int ev_flags)
{
	struct event_queue *ev_q = get_event_q();
	if (ev_flags & EVENT_VCORE_PRIVATE)
		ev_q->ev_mbox = &vcpd_of(vcoreid)->ev_mbox_private;
	else
		ev_q->ev_mbox = &vcpd_of(vcoreid)->ev_mbox_public;
	return ev_q;
}

void put_event_q(struct event_queue *ev_q)
{
	/* if we use something other than malloc, we'll need to be aware that ev_q
	 * is not an event_queue_big. */
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
 * if you want one or not.  If you want the event to go to the vcore private
 * mbox (meaning no other core should ever handle it), send in
 * EVENT_VCORE_PRIVATE with ev_flags.
 *
 * This is the simplest thing applications may want, and shows how you can put
 * the other event functions together to get similar things done. */
void enable_kevent(unsigned int ev_type, uint32_t vcoreid, int ev_flags)
{
	struct event_queue *ev_q = get_event_q_vcpd(vcoreid, ev_flags);
	ev_q->ev_flags = ev_flags;
	ev_q->ev_vcore = vcoreid;
	ev_q->ev_handler = 0;
	wmb();	/* make sure ev_q is filled out before registering */
	register_kevent_q(ev_q, ev_type);
}

/* Stop receiving the events (one could be on the way).  Caller needs to be
 * careful, since the kernel might be sending an event to the ev_q.  Depending
 * on the ev_q, it may be hard to know when it is done (for instance, if all
 * syscalls you ever registered with the ev_q are done, then it would be okay).
 * o/w, don't free it. */
struct event_queue *disable_kevent(unsigned int ev_type)
{
	return clear_kevent_q(ev_type);
}

/********* Event Handling / Reception ***********/
/* Somewhat ghetto helper, for the lazy.  If all you care about is an event
 * number, this will see if the event happened or not.  It will try for a
 * message, but if there is none, it will go for a bit.  Note that multiple
 * bit messages will turn into just one bit. */
unsigned int get_event_type(struct event_mbox *ev_mbox)
{
	struct event_msg local_msg = {0};
	/* UCQ returns 0 on success, so this will dequeue and return the type. */
	if (!get_ucq_msg(&ev_mbox->ev_msgs, &local_msg)) {
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
 * return (don't context switch to a u_thread) */
handle_event_t ev_handlers[MAX_NR_EVENT] = {[EV_EVENT] handle_ev_ev,
                                            [EV_CHECK_MSGS] handle_check_msgs,
                                            0};

/* Attempts to handle a message.  Returns 1 if we dequeued a msg, 0 o/w. */
int handle_one_mbox_msg(struct event_mbox *ev_mbox)
{
	struct event_msg local_msg;
	unsigned int ev_type;
	/* get_ucq returns 0 on success, -1 on empty */
	if (get_ucq_msg(&ev_mbox->ev_msgs, &local_msg) == -1)
		return 0;
	ev_type = local_msg.ev_type;
	assert(ev_type < MAX_NR_EVENT);
	printd("[event] UCQ (mbox %08p), ev_type: %d\n", ev_mbox, ev_type);
	if (ev_handlers[ev_type])
		ev_handlers[ev_type](&local_msg, ev_type);
	return 1;
}

/* Handle an mbox.  This is the receive-side processing of an event_queue.  It
 * takes an ev_mbox, since the vcpd mbox isn't a regular ev_q.  Returns 1 if we
 * handled something, 0 o/w. */
int handle_mbox(struct event_mbox *ev_mbox)
{
	int retval = 0;
	uint32_t vcoreid = vcore_id();
	void bit_handler(unsigned int bit) {
		printd("[event] Bit: ev_type: %d\n", bit);
		if (ev_handlers[bit])
			ev_handlers[bit](0, bit);
		retval = 1;
		/* Consider checking the queue for incoming messages while we're here */
	}
	printd("[event] handling ev_mbox %08p on vcore %d\n", ev_mbox, vcore_id());
	/* Some stack-smashing bugs cause this to fail */
	assert(ev_mbox);
	/* Handle all full messages, tracking if we do at least one. */
	while (handle_one_mbox_msg(ev_mbox))
		retval = 1;
	/* Process all bits, if the kernel tells us any bit is set.  We don't clear
	 * the flag til after we check everything, in case one of the handlers
	 * doesn't return.  After we clear it, we recheck. */
	if (ev_mbox->ev_check_bits) {
		do {
			ev_mbox->ev_check_bits = TRUE;	/* in case we don't return */
			cmb();
			BITMASK_FOREACH_SET(ev_mbox->ev_bitmap, MAX_NR_EVENT, bit_handler,
			                    TRUE);
			ev_mbox->ev_check_bits = FALSE;
			wrmb();	/* check_bits written before we check for it being clear */
		} while (!BITMASK_IS_CLEAR(ev_mbox->ev_bitmap, MAX_NR_EVENT));
	}
	return retval;
}

/* Empty if the UCQ is empty and the bits don't need checked */
bool mbox_is_empty(struct event_mbox *ev_mbox)
{
	return (ucq_is_empty(&ev_mbox->ev_msgs) && (!ev_mbox->ev_check_bits));
}

/* The EV_EVENT handler - extract the ev_q from the message. */
void handle_ev_ev(struct event_msg *ev_msg, unsigned int ev_type)
{
	struct event_queue *ev_q;
	/* EV_EVENT can't handle not having a message / being a bit.  If we got a
	 * bit message, it's a bug somewhere */
	assert(ev_msg);
	ev_q = ev_msg->ev_arg3;
	/* Same deal, a null ev_q is probably a bug, or someone being a jackass */
	assert(ev_q);
	/* Clear pending, so we can start getting INDIRs and IPIs again.  We must
	 * set this before (compared to handle_events, then set it, then handle
	 * again), since there is no guarantee handle_event_q() will return.  If
	 * there is a pending preemption, the vcore quickly yields and will deal
	 * with the remaining events in the future - meaning it won't return to
	 * here. */
	ev_q->ev_alert_pending = FALSE;
	wmb();	/* don't let the pending write pass the signaling of an ev recv */
	handle_event_q(ev_q);
}

/* This handler tells us to check the public message box of a vcore. */
void handle_check_msgs(struct event_msg *ev_msg, unsigned int ev_type)
{
	uint32_t rem_vcoreid;
	assert(ev_msg);
	rem_vcoreid = ev_msg->ev_arg2;
	printd("[event] handle check msgs for VC %d on VC %d\n", rem_vcoreid,
	       vcore_id());
	/* if it is a message for ourselves, then we can abort.  Vcores will check
	 * their own messages via handle_events() (which either we're doing now, or
	 * will do when we are done dealing with another vcore's mbox). */
	if (rem_vcoreid == vcore_id())
		return;
	/* they should have had their can_rcv turned off at some point, though it is
	 * possible that it was turned back on by now.  we don't really care - our
	 * job is to make sure their messages get checked. */
	handle_vcpd_mbox(rem_vcoreid);
}

/* Attempts to handle events, if notif_pending.  The kernel always sets
 * notif_pending after posting a message to either public or private mailbox.
 * When this returns, as far as we are concerned, notif_pending is FALSE.
 * However, a concurrent kernel writer could have reset it to true.  This is
 * fine; whenever we leave VC ctx we double check notif_pending.  Returns 1 or 2
 * if we actually handled a message, 0 o/w.
 *
 * WARNING: this might not return and/or current_uthread may change. */
int handle_events(uint32_t vcoreid)
{
	struct preempt_data *vcpd = vcpd_of(vcoreid);
	int retval = 0;
	if (vcpd->notif_pending) {
		vcpd->notif_pending = FALSE;
		wrmb();	/* prevent future reads from happening before notif_p write */
		retval += handle_mbox(&vcpd->ev_mbox_private);
		retval += handle_mbox(&vcpd->ev_mbox_public);
	}
	return retval;
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
	printd("[event] handling ev_q %08p on vcore %d\n", ev_q, vcore_id());
	/* Raw ev_qs that haven't been connected to an mbox, user bug: */
	assert(ev_q->ev_mbox);
	handle_mbox(ev_q->ev_mbox);
}

/* Sends the calling vcore a message to its public mbox.  This is purposefully
 * limited to just the calling vcore, since in future versions, we can send via
 * ucqs directly (in many cases).  That will require the caller to be the
 * vcoreid, due to some preemption recovery issues (another ucq poller is
 * waiting on us when we got preempted, and we never up nr_cons). */
void send_self_vc_msg(struct event_msg *ev_msg)
{
	// TODO: try to use UCQs (requires additional support)
	/* ev_type actually gets ignored currently.  ev_msg is what matters if it is
	 * non-zero.  FALSE means it's going to the public mbox */
	sys_self_notify(vcore_id(), ev_msg->ev_type, ev_msg, FALSE);
}

/* Helper: makes the current core handle a remote vcore's VCPD public mbox events.
 *
 * Both cases (whether we are handling someone else's already or not) use some
 * method of telling our future self what to do.  When we aren't already
 * handling it, we use TLS, and jump to vcore entry.  When we are already
 * handling, then we send a message to ourself, which we deal with when we
 * handle our own events (which is later in vcore entry).
 *
 * We need to reset the stack and deal with it in vcore entry to avoid recursing
 * deeply and running off the transition stack.  (handler calling handle event).
 *
 * Note that we might not be the one that gets the message we send.  If we pull
 * a sys_change_to, someone else might be polling our public message box.  All
 * we're doing is making sure that we don't forget to check rem_vcoreid's mbox.
 *
 * Finally, note that this function might not return.  However, it'll handle the
 * details related to vcpd mboxes, so you don't use the ev_might_not_return()
 * helpers with this. */
void handle_vcpd_mbox(uint32_t rem_vcoreid)
{
	uint32_t vcoreid = vcore_id();
	struct preempt_data *vcpd = vcpd_of(vcoreid);
	struct event_msg local_msg = {0};
	assert(vcoreid != rem_vcoreid);			/* this shouldn't happen */
	/* If they are empty, then we're done */
	if (mbox_is_empty(&vcpd_of(rem_vcoreid)->ev_mbox_public))
		return;
	if (__vc_handle_an_mbox) {
		/* we might be already handling them, in which case, abort */
		if (__vc_rem_vcoreid == rem_vcoreid)
			return;
		/* Already handling message for someone, need to send ourselves a
		 * message to check rem_vcoreid, which we'll process later. */
		local_msg.ev_type = EV_CHECK_MSGS;
		local_msg.ev_arg2 = rem_vcoreid;	/* 32bit arg */
		send_self_vc_msg(&local_msg);
		return;
	}
	/* No return after here */
	/* At this point, we aren't in the process of handling someone else's
	 * messages, so just tell our future self what to do */
	__vc_handle_an_mbox = TRUE;
	__vc_rem_vcoreid = rem_vcoreid;
	/* Reset the stack and start over in vcore context */
	set_stack_pointer((void*)vcpd->transition_stack);
	vcore_entry();
	assert(0);
}

/* Handle remote vcpd public mboxes, if that's what we want to do.  Call this
 * from vcore entry, pairs with handle_vcpd_mbox(). */
void try_handle_remote_mbox(void)
{
	if (__vc_handle_an_mbox) {
		handle_mbox(&vcpd_of(__vc_rem_vcoreid)->ev_mbox_public);
		/* only clear the flag when we have returned from handling messages.  if
		 * an event handler (like preempt_recover) doesn't return, we'll clear
		 * this flag elsewhere. (it's actually not a big deal if we don't). */
		cmb();
		__vc_handle_an_mbox = FALSE;
	}
}

/* Event handler helpers */

/* For event handlers that might not return, we need to call this before the
 * command that might not return.  In the event we were handling a remote
 * vcore's messages, it'll send ourselves a messages that we (or someone who
 * polls us) will get so that someone finishes off that vcore's messages).
 * Doesn't matter who does, so long as someone does.
 *
 * This returns whether or not we were handling someone's messages.  Pass the
 * parameter to ev_we_returned() */
bool ev_might_not_return(void)
{
	struct event_msg local_msg = {0};
	bool were_handling_remotes = FALSE;
	if (__vc_handle_an_mbox) {
		/* slight chance we finished with their mbox (were on the last one) */
		if (!mbox_is_empty(&vcpd_of(__vc_rem_vcoreid)->ev_mbox_public)) {
			/* But we aren't, so we'll need to send a message */
			local_msg.ev_type = EV_CHECK_MSGS;
			local_msg.ev_arg2 = __vc_rem_vcoreid;	/* 32bit arg */
			send_self_vc_msg(&local_msg);
		}
		/* Either way, we're not working on this one now.  Note this is more of
		 * an optimization - it'd be harmless (I think) to poll another vcore's
		 * pub mbox once when we pop up in vc_entry in the future */
		__vc_handle_an_mbox = FALSE;
		return TRUE;
	}
	return FALSE;
}

/* Call this when you return, paired up with ev_might_not_return().  If
 * ev_might_not_return turned off uth_handle, we'll turn it back on. */
void ev_we_returned(bool were_handling_remotes)
{
	if (were_handling_remotes)
		__vc_handle_an_mbox = TRUE;
}

/* Debugging */
void print_ev_msg(struct event_msg *msg)
{
	printf("MSG at %08p\n", msg);
	printf("\ttype: %d\n", msg->ev_type);
	printf("\targ1 (16): 0x%4x\n", msg->ev_arg1);
	printf("\targ2 (32): 0x%8x\n", msg->ev_arg2);
	printf("\targ3 (32): 0x%8x\n", msg->ev_arg3);
	printf("\targ4 (64): 0x%16x\n", msg->ev_arg4);
}
