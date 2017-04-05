/* Copyright (c) 2011-2014 The Regents of the University of California
 * Copyright (c) 2015 Google Inc
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Userspace utility functions for receiving events and notifications (IPIs).
 * Some are higher level than others; just use what you need. */ 

#include <ros/event.h>
#include <ros/procdata.h>
#include <parlib/ucq.h>
#include <parlib/evbitmap.h>
#include <parlib/ceq.h>
#include <parlib/vcore.h>
#include <stdlib.h>
#include <string.h>
#include <parlib/assert.h>
#include <errno.h>
#include <parlib/parlib.h>
#include <parlib/event.h>
#include <parlib/uthread.h>
#include <parlib/spinlock.h>
#include <parlib/mcs.h>
#include <parlib/poke.h>
#include <sys/queue.h>
#include <malloc.h>

/* For remote VCPD mbox event handling */
__thread bool __vc_handle_an_mbox = FALSE;
__thread uint32_t __vc_rem_vcoreid;

/********* Event_q Setup / Registration  ***********/

/* Get event_qs via these interfaces, since eventually we'll want to either
 * allocate from pinned memory or use some form of a slab allocator.  Also,
 * these stitch up the big_q so its ev_mbox points to its internal mbox.  Never
 * access the internal mbox directly.
 *
 * Raw ones need to have their mailboxes initialized.  If you're making a lot of
 * these and they perform their own mmaps (e.g. UCQs), you can do one big mmap
 * and init the ucqs on your own, which ought to perform better.
 *
 * Use the 'regular' one for big_qs if you don't want to worry about the mbox
 * initalization */
struct event_queue *get_eventq_raw(void)
{
	/* TODO: (PIN) should be pinned memory */
	struct event_queue_big *big_q = malloc(sizeof(struct event_queue_big));
	memset(big_q, 0, sizeof(struct event_queue_big));
	big_q->ev_mbox = &big_q->ev_imbox;
	return (struct event_queue*)big_q;
}

struct event_queue *get_eventq(int mbox_type)
{
	struct event_queue *big_q = get_eventq_raw();
	event_mbox_init(big_q->ev_mbox, mbox_type);
	return big_q;
}

/* Basic initialization of a single mbox.  If you know the type, you can set up
 * the mbox manually with possibly better performance.  For instance, ucq_init()
 * calls mmap internally.  You could mmap a huge blob on your own and call
 * ucq_raw_init (don't forget to set the mbox_type!) */
void event_mbox_init(struct event_mbox *ev_mbox, int mbox_type)
{
	ev_mbox->type = mbox_type;
	switch (ev_mbox->type) {
		case (EV_MBOX_UCQ):
			ucq_init(&ev_mbox->ucq);
			break;
		case (EV_MBOX_BITMAP):
			evbitmap_init(&ev_mbox->evbm);
			break;
		case (EV_MBOX_CEQ):
			ceq_init(&ev_mbox->ceq, CEQ_OR, CEQ_DEFAULT_SZ, CEQ_DEFAULT_SZ);
			break;
		default:
			printf("Unknown mbox type %d!\n", ev_mbox->type);
			break;
	}
}

/* Give it up.  I don't recommend calling these unless you're sure the queues
 * aren't in use (unregistered, etc). (TODO: consider some checks for this) */
void put_eventq_raw(struct event_queue *ev_q)
{
	/* if we use something other than malloc, we'll need to be aware that ev_q
	 * is actually an event_queue_big.  One option is to use the flags, though
	 * this could be error prone. */
	free(ev_q);
}

void put_eventq(struct event_queue *ev_q)
{
	event_mbox_cleanup(ev_q->ev_mbox);
	put_eventq_raw(ev_q);
}

void event_mbox_cleanup(struct event_mbox *ev_mbox)
{
	switch (ev_mbox->type) {
		case (EV_MBOX_UCQ):
			ucq_free_pgs(&ev_mbox->ucq);
			break;
		case (EV_MBOX_BITMAP):
			evbitmap_cleanup(&ev_mbox->evbm);
			break;
		case (EV_MBOX_CEQ):
			ceq_cleanup(&ev_mbox->ceq);
			break;
		default:
			printf("Unknown mbox type %d!\n", ev_mbox->type);
			break;
	}
}

/* Need to point this event_q to an mbox - usually to a vcpd */
struct event_queue *get_eventq_slim(void)
{
	/* TODO: (PIN) should be pinned memory */
	struct event_queue *ev_q = malloc(sizeof(struct event_queue));
	memset(ev_q, 0, sizeof(struct event_queue));
	return ev_q;
}

/* Gets a small ev_q, with ev_mbox pointing to the vcpd mbox of vcoreid.  If
 * ev_flags has EVENT_VCORE_PRIVATE set, it'll give you the private mbox.  o/w,
 * you'll get the public one. */
struct event_queue *get_eventq_vcpd(uint32_t vcoreid, int ev_flags)
{
	struct event_queue *ev_q = get_eventq_slim();
	if (ev_flags & EVENT_VCORE_PRIVATE)
		ev_q->ev_mbox = &vcpd_of(vcoreid)->ev_mbox_private;
	else
		ev_q->ev_mbox = &vcpd_of(vcoreid)->ev_mbox_public;
	return ev_q;
}

void put_eventq_slim(struct event_queue *ev_q)
{
	/* if we use something other than malloc, we'll need to be aware that ev_q
	 * is not an event_queue_big. */
	free(ev_q);
}

void put_eventq_vcpd(struct event_queue *ev_q)
{
	put_eventq_slim(ev_q);
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
	struct event_queue *ev_q = get_eventq_vcpd(vcoreid, ev_flags);
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

	if (extract_one_mbox_msg(ev_mbox, &local_msg))
		return local_msg.ev_type;
	return EV_NONE;
}

/* Attempts to register ev_q with sysc, so long as sysc is not done/progress.
 * Returns true if it succeeded, and false otherwise.  False means that the
 * syscall is done, and does not need an event set (and should be handled
 * accordingly).
 *
 * A copy of this is in glibc/sysdeps/akaros/syscall.c.  Keep them in sync. */
bool register_evq(struct syscall *sysc, struct event_queue *ev_q)
{
	int old_flags;
	sysc->ev_q = ev_q;
	wrmb();	/* don't let that write pass any future reads (flags) */
	/* Try and set the SC_UEVENT flag (so the kernel knows to look at ev_q) */
	do {
		/* no cmb() needed, the atomic_read will reread flags */
		old_flags = atomic_read(&sysc->flags);
		/* Spin if the kernel is mucking with syscall flags */
		while (old_flags & SC_K_LOCK)
			old_flags = atomic_read(&sysc->flags);
		/* If the kernel finishes while we are trying to sign up for an event,
		 * we need to bail out */
		if (old_flags & (SC_DONE | SC_PROGRESS)) {
			sysc->ev_q = 0;		/* not necessary, but might help with bugs */
			return FALSE;
		}
	} while (!atomic_cas(&sysc->flags, old_flags, old_flags | SC_UEVENT));
	return TRUE;
}

/* De-registers a syscall, so that the kernel will not send an event when it is
 * done.  The call could already be SC_DONE, or could even finish while we try
 * to unset SC_UEVENT.
 *
 * There is a chance the kernel sent an event if you didn't do this in time, but
 * once this returns, the kernel won't send a message.
 *
 * If the kernel is trying to send a message right now, this will spin (on
 * SC_K_LOCK).  We need to make sure we deregistered, and that if a message
 * is coming, that it already was sent (and possibly overflowed), before
 * returning. */
void deregister_evq(struct syscall *sysc)
{
	int old_flags;
	sysc->ev_q = 0;
	wrmb();	/* don't let that write pass any future reads (flags) */
	/* Try and unset the SC_UEVENT flag */
	do {
		/* no cmb() needed, the atomic_read will reread flags */
		old_flags = atomic_read(&sysc->flags);
		/* Spin if the kernel is mucking with syscall flags */
		while (old_flags & SC_K_LOCK)
			old_flags = atomic_read(&sysc->flags);
		/* Note we don't care if the SC_DONE flag is getting set.  We just need
		 * to avoid clobbering flags */
	} while (!atomic_cas(&sysc->flags, old_flags, old_flags & ~SC_UEVENT));
}

/* Actual Event Handling */

/* List of handler lists, process-wide.  They all must return (don't context
 * switch to a u_thread) */
struct ev_handler *ev_handlers[MAX_NR_EVENT] = {0};
spinpdrlock_t ev_h_wlock = SPINPDR_INITIALIZER;

int register_ev_handler(unsigned int ev_type, handle_event_t handler,
                        void *data)
{
	/* Nasty uthread code assumes this was malloced */
	struct ev_handler *new_h = malloc(sizeof(struct ev_handler));

	if (!new_h)
		return -1;
	new_h->func = handler;
	new_h->data = data;
	spin_pdr_lock(&ev_h_wlock);
	new_h->next = ev_handlers[ev_type];
	wmb();	/* make sure new_h is done before publishing to readers */
	ev_handlers[ev_type] = new_h;
	spin_pdr_unlock(&ev_h_wlock);
	return 0;
}

int deregister_ev_handler(unsigned int ev_type, handle_event_t handler,
                          void *data)
{
	/* TODO: User-level RCU */
	printf("Failed to dereg handler, not supported yet!\n");
	return -1;
}

static void run_ev_handlers(unsigned int ev_type, struct event_msg *ev_msg)
{
	struct ev_handler *handler;
	/* TODO: RCU read lock */
	handler = ev_handlers[ev_type];
	while (handler) {
		handler->func(ev_msg, ev_type, handler->data);
		handler = handler->next;
	}
}

/* Attempts to extract a message from an mbox, copying it into ev_msg.
 * Returns TRUE on success. */
bool extract_one_mbox_msg(struct event_mbox *ev_mbox, struct event_msg *ev_msg)
{
	switch (ev_mbox->type) {
		case (EV_MBOX_UCQ):
			return get_ucq_msg(&ev_mbox->ucq, ev_msg);
		case (EV_MBOX_BITMAP):
			return get_evbitmap_msg(&ev_mbox->evbm, ev_msg);
		case (EV_MBOX_CEQ):
			return get_ceq_msg(&ev_mbox->ceq, ev_msg);
		default:
			printf("Unknown mbox type %d!\n", ev_mbox->type);
			return FALSE;
	}
}

/* Attempts to handle a message.  Returns 1 if we dequeued a msg, 0 o/w. */
int handle_one_mbox_msg(struct event_mbox *ev_mbox)
{
	struct event_msg local_msg;
	unsigned int ev_type;
	/* extract returns TRUE on success, we return 1. */
	if (!extract_one_mbox_msg(ev_mbox, &local_msg))
		return 0;
	ev_type = local_msg.ev_type;
	assert(ev_type < MAX_NR_EVENT);
	printd("[event] UCQ (mbox %08p), ev_type: %d\n", ev_mbox, ev_type);
	run_ev_handlers(ev_type, &local_msg);
	return 1;
}

/* Handle an mbox.  This is the receive-side processing of an event_queue.  It
 * takes an ev_mbox, since the vcpd mbox isn't a regular ev_q.  Returns 1 if we
 * handled something, 0 o/w. */
int handle_mbox(struct event_mbox *ev_mbox)
{
	int retval = 0;
	printd("[event] handling ev_mbox %08p on vcore %d\n", ev_mbox, vcore_id());
	/* Some stack-smashing bugs cause this to fail */
	assert(ev_mbox);
	/* Handle all full messages, tracking if we do at least one. */
	while (handle_one_mbox_msg(ev_mbox))
		retval = 1;
	return retval;
}

/* Empty if the UCQ is empty and the bits don't need checked */
bool mbox_is_empty(struct event_mbox *ev_mbox)
{
	switch (ev_mbox->type) {
		case (EV_MBOX_UCQ):
			return ucq_is_empty(&ev_mbox->ucq);
		case (EV_MBOX_BITMAP):
			return evbitmap_is_empty(&ev_mbox->evbm);
		case (EV_MBOX_CEQ):
			return ceq_is_empty(&ev_mbox->ceq);
		default:
			printf("Unknown mbox type %d!\n", ev_mbox->type);
			return FALSE;
	}
}

/* The EV_EVENT handler - extract the ev_q from the message. */
void handle_ev_ev(struct event_msg *ev_msg, unsigned int ev_type, void *data)
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

/* Handles VCPD events (public and private).  The kernel always sets
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
	vcpd->notif_pending = FALSE;
	wrmb();	/* prevent future reads from happening before notif_p write */
	retval += handle_mbox(&vcpd->ev_mbox_private);
	retval += handle_mbox(&vcpd->ev_mbox_public);
	return retval;
}

/* Handles the events on ev_q IAW the event_handlers[].  If the ev_q is
 * application specific, then this will dispatch/handle based on its flags. */
void handle_event_q(struct event_queue *ev_q)
{
	printd("[event] handling ev_q %08p on vcore %d\n", ev_q, vcore_id());
	/* If the program wants to handle the ev_q on its own: */
	if (ev_q->ev_handler) {
		/* Remember this can't block or page fault */
		ev_q->ev_handler(ev_q);
		return;
	}
	/* Raw ev_qs that haven't been connected to an mbox, user bug: */
	assert(ev_q->ev_mbox);
	/* The "default" ev_handler, common enough that I don't want a func ptr */
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
	set_stack_pointer((void*)vcpd->vcore_stack);
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

/* Uthreads blocking on event queues
 *
 * It'd be nice to have a uthread sleep until an event queue has some activity
 * (e.g. a new message).  It'd also be nice to wake up early with a timer.  It
 * is tempting to try something like an INDIR and have one evq multiplex two
 * others (the real event and an alarm).  But then you can't separate the two
 * streams; what if one thread sleeps on just the event at the same time?  What
 * if we want to support something like Go's select: a thread wants to block
 * until there is some activity on some channel?
 *
 * Ultimately, we want to allow M uthreads to block on possibly different
 * subsets of N event queues.
 *
 * Every uthread will have a sleep controller, and every event queue will have a
 * wakeup controller.  There are up to MxN linkage structures connecting these.
 *
 * We'll use the event_queue handler to override the default event processing.
 * This means the event queues that are used for blocking uthreads can *only* be
 * used for that; the regular event processing will not happen.  This is mostly
 * true.  It is possible to extract events from an evq's mbox concurrently.
 *
 * I briefly considered having one global lock to protect all of the lists and
 * structures.  That's lousy for the obvious scalability reason, but it seemed
 * like it'd make things easier, especially when I thought I needed locks in
 * both the ectlr and the uctlr (in early versions, I considered having the
 * handler yank itself out of the ectlr, copying a message into that struct, or
 * o/w needing protection).  On occasion, we run into the "I'd like to split my
 * lock between two components and still somehow synchronize" issue (e.g. FD
 * taps, with the FDT lock and the blocking/whatever that goes on in a device).
 * Whenever that comes up, we usually can get some help from other shared memory
 * techniques.  For FD taps, it's the kref.  For us, it's post-and-poke, though
 * it didn't solve all of our problems - I use it as a tool with some basic
 * shared memory signalling. */

struct evq_wait_link;
TAILQ_HEAD(wait_link_tailq, evq_wait_link);

/* Bookkeeping for the uthread sleeping on a bunch of event queues.
 *
 * Notes on concurrency: most fields are not protected.  check_evqs is racy, and
 * written to by handlers.  The tailq is only used by the uthread.  blocked is
 * never concurrently *written*; see __uth_wakeup_poke() for details. */
struct uth_sleep_ctlr {
	struct uthread				*uth;
	struct spin_pdr_lock		in_use;
	bool						check_evqs;
	bool						blocked;
	struct poke_tracker			poker;
	struct wait_link_tailq		evqs;
};

/* Attaches to an event_queue (ev_udata), tracks the uthreads for this evq */
struct evq_wakeup_ctlr {
	/* If we ever use a sync_obj, that would replace waiters.  But also note
	 * that we want a pointer to something other than the uthread, and currently
	 * we also wake all threads - there's no scheduling decision. */
	struct wait_link_tailq		waiters;
	struct spin_pdr_lock		lock;
};

/* Up to MxN of these, N of them per uthread. */
struct evq_wait_link {
	struct uth_sleep_ctlr		*uth_ctlr;
	TAILQ_ENTRY(evq_wait_link)	link_uth;
	struct evq_wakeup_ctlr		*evq_ctlr;
	TAILQ_ENTRY(evq_wait_link)	link_evq;
};

/* Poke function: ensures the uth managed by uctlr wakes up.  poke() ensures
 * there is only one thread in this function at a time.  However, it could be
 * called spuriously, which is why we check 'blocked.' */
static void __uth_wakeup_poke(void *arg)
{
	struct uth_sleep_ctlr *uctlr = arg;
	/* There are no concurrent writes to 'blocked'.  Blocked is only ever
	 * written when the uth sleeps and only ever cleared here.  Once the uth
	 * writes it, it does not write it again until after we clear it.
	 *
	 * This is still racy - we could see !blocked, then blocked gets set.  In
	 * that case, the poke failed, and that is harmless.  The uth will see
	 * 'check_evqs', which was set before poke, which would be before writing
	 * blocked, and the uth checks 'check_evqs' after writing. */
	if (uctlr->blocked) {
		uctlr->blocked = FALSE;
		cmb();	/* clear blocked before starting the uth */
		uthread_runnable(uctlr->uth);
	}
}

static void uth_sleep_ctlr_init(struct uth_sleep_ctlr *uctlr,
                                struct uthread *uth)
{
	uctlr->uth = uth;
	spin_pdr_init(&uctlr->in_use);
	uctlr->check_evqs = FALSE;
	uctlr->blocked = FALSE;
	poke_init(&uctlr->poker, __uth_wakeup_poke);
	TAILQ_INIT(&uctlr->evqs);
}

/* This handler runs when the ev_q is checked.  Instead of doing anything with
 * the ev_q, we make sure that every uthread that was waiting on us wakes up.
 * The uthreads could be waiting on several evqs, so there could be multiple
 * independent wake-up attempts, hence the poke.  Likewise, the uthread could be
 * awake when we poke.  The uthread will check check_evqs after sleeping, in
 * case we poke before it blocks (and the poke fails).
 *
 * Also, there could be concurrent callers of this handler, and other uthreads
 * signing up for a wakeup. */
void evq_wakeup_handler(struct event_queue *ev_q)
{
	struct evq_wakeup_ctlr *ectlr = ev_q->ev_udata;
	struct evq_wait_link *i;
	assert(ectlr);
	spin_pdr_lock(&ectlr->lock);
	/* Note we wake up all sleepers, even though only one is likely to get the
	 * message.  See the notes in unlink_ectlr() for more info. */
	TAILQ_FOREACH(i, &ectlr->waiters, link_evq) {
		i->uth_ctlr->check_evqs = TRUE;
		cmb();	/* order check write before poke (poke has atomic) */
		poke(&i->uth_ctlr->poker, i->uth_ctlr);
	}
	spin_pdr_unlock(&ectlr->lock);
}

/* Helper, attaches a wakeup controller to the event queue. */
void evq_attach_wakeup_ctlr(struct event_queue *ev_q)
{
	struct evq_wakeup_ctlr *ectlr = malloc(sizeof(struct evq_wakeup_ctlr));
	memset(ectlr, 0, sizeof(struct evq_wakeup_ctlr));
	spin_pdr_init(&ectlr->lock);
	TAILQ_INIT(&ectlr->waiters);
	ev_q->ev_udata = ectlr;
	ev_q->ev_handler = evq_wakeup_handler;
}

void evq_remove_wakeup_ctlr(struct event_queue *ev_q)
{
	free(ev_q->ev_udata);
	ev_q->ev_udata = 0;
	ev_q->ev_handler = 0;
}

static void link_uctlr_ectlr(struct uth_sleep_ctlr *uctlr,
                             struct evq_wakeup_ctlr *ectlr,
                             struct evq_wait_link *link)
{
	/* No lock needed for the uctlr; we're the only one modifying evqs */
	link->uth_ctlr = uctlr;
	TAILQ_INSERT_HEAD(&uctlr->evqs, link, link_uth);
	/* Once we add ourselves to the ectrl list, we could start getting poked */
	link->evq_ctlr = ectlr;
	spin_pdr_lock(&ectlr->lock);
	TAILQ_INSERT_HEAD(&ectlr->waiters, link, link_evq);
	spin_pdr_unlock(&ectlr->lock);
}

/* Disconnects us from a wakeup controller.
 *
 * Our evq handlers wake up *all* uthreads that are waiting for activity
 * (broadcast).  It's a tradeoff.  If the list of uthreads is long, then it is
 * wasted effort.  An alternative is to wake up exactly one, with slightly
 * greater overheads.  In the exactly-one case, multiple handlers could wake
 * this uth up at once, but we can only extract one message.  If we do the
 * single wake up, then when we detach from an ectlr, we need to peak in the
 * mbox to see if it is not empty, and conditionally run its handler again, such
 * that no uthread sits on a ectlr that has activity/pending messages (in
 * essence, level triggered). */
static void unlink_ectlr(struct evq_wait_link *link)
{
	struct evq_wakeup_ctlr *ectlr = link->evq_ctlr;
	spin_pdr_lock(&ectlr->lock);
	TAILQ_REMOVE(&ectlr->waiters, link, link_evq);
	spin_pdr_unlock(&ectlr->lock);
}

/* Helper: polls all evqs once and extracts the first message available.  The
 * message is copied into ev_msg, and the evq with the activity is copied into
 * which_evq (if it is non-zero).  Returns TRUE on success. */
static bool extract_evqs_msg(struct event_queue *evqs[], size_t nr_evqs,
                             struct event_msg *ev_msg,
                             struct event_queue **which_evq)
{
	struct event_queue *evq_i;
	bool ret = FALSE;
	/* We need to have notifs disabled when extracting messages from some
	 * mboxes.  Many mboxes have some form of busy waiting between consumers
	 * (userspace).  If we're just a uthread, we could wind up on a runqueue
	 * somewhere while someone else spins, possibly in VC ctx. */
	uth_disable_notifs();
	for (int i = 0; i < nr_evqs; i++) {
		evq_i = evqs[i];
		if (extract_one_mbox_msg(evq_i->ev_mbox, ev_msg)) {
			if (which_evq)
				*which_evq = evq_i;
			ret = TRUE;
			break;
		}
	}
	uth_enable_notifs();
	return ret;
}

/* Yield callback */
static void __uth_blockon_evq_cb(struct uthread *uth, void *arg)
{
	struct uth_sleep_ctlr *uctlr = arg;

	uthread_has_blocked(uth, NULL, UTH_EXT_BLK_EVENTQ);
	cmb();	/* actually block before saying 'blocked' */
	uctlr->blocked = TRUE;	/* can be woken up now */
	wrmb();	/* write 'blocked' before read 'check_evqs' */
	/* If someone set check_evqs, we should wake up.  We're competing with other
	 * wakers via poke (we may have already woken up!). */
	if (uctlr->check_evqs)
		poke(&uctlr->poker, uctlr);
	/* Once we say we're blocked, we could be woken up (possibly by our poke
	 * here) and the uthread could run on another core.  Holding this lock
	 * prevents the uthread from quickly returning and freeing the memory of
	 * uctrl before we have a chance to check_evqs or poke. */
	spin_pdr_unlock(&uctlr->in_use);
}

/* Direct version, with *evqs[]. */
void uth_blockon_evqs_arr(struct event_msg *ev_msg,
                          struct event_queue **which_evq,
                          struct event_queue *evqs[], size_t nr_evqs)
{
	struct uth_sleep_ctlr uctlr;
	struct evq_wait_link linkage[nr_evqs];

	/* Catch user mistakes.  If they lack a handler, they didn't attach.  They
	 * are probably using our evq_wakeup_handler, but they might have their own
	 * wrapper function. */
	for (int i = 0; i < nr_evqs; i++)
		assert(evqs[i]->ev_handler);
	/* Check for activity on the evqs before going through the hassle of
	 * sleeping.  ("check, signal, check again" pattern). */
	if (extract_evqs_msg(evqs, nr_evqs, ev_msg, which_evq))
		return;
	uth_sleep_ctlr_init(&uctlr, current_uthread);
	memset(linkage, 0, sizeof(struct evq_wait_link) * nr_evqs);
	for (int i = 0; i < nr_evqs; i++)
		link_uctlr_ectlr(&uctlr, (struct evq_wakeup_ctlr*)evqs[i]->ev_udata,
		                 &linkage[i]);
	/* Mesa-style sleep until we get a message.  Mesa helps a bit here, since we
	 * can just deregister from them all when we're done.  o/w it is tempting to
	 * have us deregister from *the* one in the handler and extract the message
	 * there; which can be tricky and harder to reason about. */
	while (1) {
		/* We need to make sure only one 'version/ctx' of this thread is active
		 * at a time.  Later on, we'll unlock in vcore ctx on the other side of
		 * a yield.  We could restart from the yield, return, and free the uctlr
		 * before that ctx has a chance to finish. */
		spin_pdr_lock(&uctlr.in_use);
		/* We're signed up.  We might already have been told to check the evqs,
		 * or there could be messages still sitting in the evqs.  check_evqs is
		 * only ever cleared here, and only ever set in evq handlers. */
		uctlr.check_evqs = FALSE;
		cmb();	/* look for messages after clearing check_evqs */
		if (extract_evqs_msg(evqs, nr_evqs, ev_msg, which_evq))
			break;
		uthread_yield(TRUE, __uth_blockon_evq_cb, &uctlr);
	}
	/* On the one hand, it's not necessary to unlock, since the memory will be
	 * freed.  But we do need to go through the process to turn on notifs and
	 * adjust the notif_disabled_depth for the case where we don't yield. */
	spin_pdr_unlock(&uctlr.in_use);
	for (int i = 0; i < nr_evqs; i++)
		unlink_ectlr(&linkage[i]);
}

/* ... are event_queue *s, nr_evqs of them.  This will block until it can
 * extract some message from one of evqs.  The message will be placed in ev_msg,
 * and the particular evq it extracted it from will be placed in which_evq, if
 * which is non-zero. */
void uth_blockon_evqs(struct event_msg *ev_msg, struct event_queue **which_evq,
                      size_t nr_evqs, ...)
{
	struct event_queue *evqs[nr_evqs];
	va_list va;
	va_start(va, nr_evqs);
	for (int i = 0; i < nr_evqs; i++)
		evqs[i] = va_arg(va, struct event_queue *);
	va_end(va);
	uth_blockon_evqs_arr(ev_msg, which_evq, evqs, nr_evqs);
}

/* ... are event_queue *s, nr_evqs of them.  This will attempt to extract some
 * message from one of evqs.  The message will be placed in ev_msg, and the
 * particular evq it extracted it from will be placed in which_evq.  Returns
 * TRUE if it extracted a message. */
bool uth_check_evqs(struct event_msg *ev_msg, struct event_queue **which_evq,
                    size_t nr_evqs, ...)
{
	struct event_queue *evqs[nr_evqs];
	va_list va;
	va_start(va, nr_evqs);
	for (int i = 0; i < nr_evqs; i++)
		evqs[i] = va_arg(va, struct event_queue *);
	va_end(va);
	return extract_evqs_msg(evqs, nr_evqs, ev_msg, which_evq);
}
