/* Copyright (c) 2011 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Kernel utility functions for sending events and notifications (IPIs) to
 * processes. */

#include <ucq.h>
#include <ceq.h>
#include <bitmask.h>
#include <event.h>
#include <atomic.h>
#include <process.h>
#include <smp.h>
#include <umem.h>
#include <stdio.h>
#include <assert.h>
#include <pmap.h>
#include <schedule.h>

/* Userspace could give us a vcoreid that causes us to compute a vcpd that is
 * outside procdata.  If we hit UWLIM, then we've gone farther than we should.
 * We check the vcoreid, instead of the resulting address, to avoid issues like
 * address wrap-around. */
static bool vcoreid_is_safe(uint32_t vcoreid)
{
	/* MAX_NUM_VCORES == MAX_NUM_CORES (check procinfo/procdata) */
	return vcoreid < MAX_NUM_CORES;
}

/* Note these three helpers return the user address of the mbox, not the KVA.
 * Load current to access this, and it will work for any process. */
static struct event_mbox *get_vcpd_mbox_priv(uint32_t vcoreid)
{
	return &__procdata.vcore_preempt_data[vcoreid].ev_mbox_private;
}

static struct event_mbox *get_vcpd_mbox_pub(uint32_t vcoreid)
{
	return &__procdata.vcore_preempt_data[vcoreid].ev_mbox_public;
}

static struct event_mbox *get_vcpd_mbox(uint32_t vcoreid, int ev_flags)
{
	if (ev_flags & EVENT_VCORE_PRIVATE)
		return get_vcpd_mbox_priv(vcoreid);
	else
		return get_vcpd_mbox_pub(vcoreid);
}

/* Can we message the vcore?  (Will it check its messages).  Note this checks
 * procdata via the user pointer. */
static bool can_msg_vcore(uint32_t vcoreid)
{
	struct preempt_data *vcpd = &__procdata.vcore_preempt_data[vcoreid];
	return atomic_read(&vcpd->flags) & VC_CAN_RCV_MSG;
}

/* Says a vcore can be messaged.  Only call this once you are sure this is true
 * (holding the proc_lock, etc). */
static void set_vcore_msgable(uint32_t vcoreid)
{
	struct preempt_data *vcpd = &__procdata.vcore_preempt_data[vcoreid];
	atomic_or(&vcpd->flags, VC_CAN_RCV_MSG);
}

static void send_evbitmap_msg(struct evbitmap *evbm, struct event_msg *msg)
{
	SET_BITMASK_BIT_ATOMIC(evbm->bitmap, msg->ev_type);
	wmb();
	evbm->check_bits = TRUE;
}

/* Posts a message to the mbox.  mbox is a pointer to user-accessible memory.
 * If mbox is a user-provided pointer, make sure that you've checked it.
 * Regardless make sure you have that process's address space loaded. */
static void post_ev_msg(struct proc *p, struct event_mbox *mbox,
                        struct event_msg *msg, int ev_flags)
{
	printd("[kernel] Sending event type %d to mbox %p\n",
	       msg->ev_type, mbox);
	/* Sanity check */
	assert(p);
	switch (mbox->type) {
	case (EV_MBOX_UCQ):
		send_ucq_msg(&mbox->ucq, p, msg);
		break;
	case (EV_MBOX_BITMAP):
		send_evbitmap_msg(&mbox->evbm, msg);
		break;
	case (EV_MBOX_CEQ):
		send_ceq_msg(&mbox->ceq, p, msg);
		break;
	default:
		printk("[kernel] Unknown mbox type %d!\n", mbox->type);
	}
}

/* Helper: use this when sending a message to a VCPD mbox.  It just posts to the
 * ev_mbox and sets notif pending.  Note this uses a userspace address for the
 * VCPD (though not a user's pointer). */
static void post_vc_msg(struct proc *p, uint32_t vcoreid,
                        struct event_mbox *ev_mbox, struct event_msg *ev_msg,
                        int ev_flags)
{
	struct preempt_data *vcpd = &__procdata.vcore_preempt_data[vcoreid];
	post_ev_msg(p, ev_mbox, ev_msg, ev_flags);
	/* Set notif pending so userspace doesn't miss the message while
	 * yielding */
	wmb(); /* Ensure ev_msg write is before notif_pending */
	/* proc_notify() also sets this, but the ev_q might not have requested
	 * an IPI, so we have to do it here too. */
	vcpd->notif_pending = TRUE;
}

/* Helper: will IPI / proc_notify if the flags say so.  We also check to make
 * sure it is mapped (slight optimization) */
static void try_notify(struct proc *p, uint32_t vcoreid, int ev_flags)
{
	/* Note this is an unlocked-peek at the vcoremap */
	if ((ev_flags & EVENT_IPI) && vcore_is_mapped(p, vcoreid))
		proc_notify(p, vcoreid);
}

/* Helper: sends the message and an optional IPI to the vcore.  Sends to the
 * public mbox. */
static void spam_vcore(struct proc *p, uint32_t vcoreid,
                       struct event_msg *ev_msg, int ev_flags)
{
	post_vc_msg(p, vcoreid, get_vcpd_mbox_pub(vcoreid), ev_msg, ev_flags);
	try_notify(p, vcoreid, ev_flags);
}

/* Attempts to message a vcore that may or may not have VC_CAN_RCV_MSG set.  If
 * so, we'll post the message and the message will eventually get dealt with
 * (when the vcore runs or when it is preempte-recovered). */
static bool try_spam_vcore(struct proc *p, uint32_t vcoreid,
                           struct event_msg *ev_msg, int ev_flags)
{
	/* Not sure if we can or not, so check before spamming.  Technically,
	 * the only critical part is that we __alert, then check can_alert. */
	if (can_msg_vcore(vcoreid)) {
		spam_vcore(p, vcoreid, ev_msg, ev_flags);
		/* prev write (notif_pending) must come before following reads*/
		wrmb();
		if (can_msg_vcore(vcoreid))
			return TRUE;
	}
	return FALSE;
}

/* Helper: will try to message (INDIR/IPI) a list member (lists of vcores).  We
 * use this on the online and bulk_preempted vcore lists.  If this succeeds in
 * alerting a vcore on the list, it'll return TRUE.  We need to be careful here,
 * since we're reading a list that could be concurrently modified.  The
 * important thing is that we can always fail if we're unsure (such as with
 * lists being temporarily empty).  The caller will be able to deal with it via
 * the ultimate fallback. */
static bool spam_list_member(struct vcore_tailq *list, struct proc *p,
                             struct event_msg *ev_msg, int ev_flags)
{
	struct vcore *vc, *vc_first;
	uint32_t vcoreid;
	int loops = 0;
	vc = TAILQ_FIRST(list);
	/* If the list appears empty, we'll bail out (failing) after the loop.
	 */
	while (vc) {
		vcoreid = vcore2vcoreid(p, vc);
		/* post the alert.  Not using the try_spam_vcore() helper since
		 * I want something more customized for the lists. */
		spam_vcore(p, vcoreid, ev_msg, ev_flags);
		/* prev write (notif_pending) must come before following reads*/
		wrmb();
		/* I used to check can_msg_vcore(vcoreid) here, but that would
		 * make spamming list members unusable for MUST_RUN scenarios.
		 *
		 * Regardless, if they are still the first on the list, then
		 * they are still going to get the message.  For the online
		 * list, proc_yield() will return them to userspace (where they
		 * will get the message) because __alert_vcore() set
		 * notif_pending.  For the BP list, they will either be turned
		 * on later, or have a preempt message sent about their demise.
		 *
		 * We race on list membership (and not exclusively
		 * VC_CAN_RCV_MSG, so that when it fails we can get a new vcore
		 * to try (or know WHP there are none). */
		vc_first = TAILQ_FIRST(list);
		if (vc == vc_first)
			return TRUE;
		/* At this point, the list has changed and the vcore we tried
		 * yielded, so we try the *new* list head.  Track loops for
		 * sanity reasons. */
		if (loops++ > 10) {
			warn("Too many (%d) attempts to find a vcore, failing!",
			     loops);
			return FALSE;	/* always safe to fail! */
		}
		/* Get set up for your attack run! */
		vc = vc_first;
	}
	return FALSE;
}

/* This makes sure ev_msg is sent to some vcore, preferring vcoreid.
 *
 * One of the goals of SPAM_INDIR (and this func) is to allow processes to yield
 * cores without fear of losing messages.  Even when yielding and getting
 * preempted, if your message is spammed, it will get to some vcore.  If
 * MUST_RUN is set, it'll get to a running vcore.  Messages that you send like
 * this must be able to handle spurious reads, since more than one vcore is
 * likely to get the message and handle it.
 *
 * We try the desired vcore, using VC_CAN_RCV_MSG.  Failing that, we'll search
 * the online and then the bulk_preempted lists.  These lists serve as a way to
 * find likely messageable vcores.  spam_list_member() helps us with them,
 * failing if anything seems to go wrong.  At which point we just lock and try
 * to deal with things.  In that scenario, we most likely would need to lock
 * anyway to wake up the process (was WAITING).
 *
 * One tricky thing with sending to the bulk_preempt list is that we may want to
 * send a message about a (bulk) preemption to someone on that list.  This works
 * since a given vcore that was preempted will be removed from that list before
 * we try to send_event() (in theory, there isn't code that can send that event
 * yet).  Someone else will get the event and wake up the preempted vcore. */
static void spam_public_msg(struct proc *p, struct event_msg *ev_msg,
			    uint32_t vcoreid, int ev_flags)
{
	struct vcore *vc;
	if (!__proc_is_mcp(p)) {
		spam_vcore(p, 0, ev_msg, ev_flags);
		return;
	}
	if (ev_flags & EVENT_VCORE_MUST_RUN) {
		/* Could check for waiting and skip these spams, which will
		 * fail.  Could also skip trying for vcoreid, and just spam any
		 * old online VC. */
		if (vcore_is_mapped(p, vcoreid)) {
			/* check, signal, check again */
			spam_vcore(p, vcoreid, ev_msg, ev_flags);
			/* notif_pending write must come before following read
			 */
			wrmb();
			if (vcore_is_mapped(p, vcoreid))
				return;
		}
		if (spam_list_member(&p->online_vcs, p, ev_msg, ev_flags))
			return;
		goto ultimate_fallback;
	}
	/* First, try posting to the desired vcore */
	if (try_spam_vcore(p, vcoreid, ev_msg, ev_flags))
		return;
	/* If the process is WAITING, let's just jump to the fallback */
	if (p->state == PROC_WAITING)
		goto ultimate_fallback;
	/* If we're here, the desired vcore is unreachable, but the process is
	 * probably RUNNING_M (online_vs) or RUNNABLE_M (bulk preempted or
	 * recently woken up), so we'll need to find another vcore. */
	if (spam_list_member(&p->online_vcs, p, ev_msg, ev_flags))
		return;
	if (spam_list_member(&p->bulk_preempted_vcs, p, ev_msg, ev_flags))
		return;
	/* Last chance, let's check the head of the inactives.  It might be
	 * alertable (the kernel set it earlier due to an event, or it was a
	 * bulk_preempt that didn't restart), and we can avoid grabbing the
	 * proc_lock. */
	vc = TAILQ_FIRST(&p->inactive_vcs);
	if (vc) {	/* might be none in rare circumstances */
		if (try_spam_vcore(p, vcore2vcoreid(p, vc), ev_msg, ev_flags)) {
			/* It's possible that we're WAITING here.  EVENT_WAKEUP
			 * will handle it.  One way for this to happen is if a
			 * normal vcore was preempted right as another vcore was
			 * yielding, and the preempted message was sent after
			 * the last vcore yielded (which caused us to be
			 * WAITING). */
			return;
		}
	}
ultimate_fallback:
	/* At this point, we can't find one.  This could be due to a (hopefully
	 * rare) weird yield/request storm, or more commonly because the lists
	 * were empty and the process is simply WAITING (yielded all of its
	 * vcores and is waiting on an event).  Time for the ultimate fallback:
	 * locking.  Note that when we __alert_vcore(), there is a chance we
	 * need to mmap, which grabs the vmr_lock and pte_lock. */
	spin_lock(&p->proc_lock);
	if (p->state != PROC_WAITING) {
		/* We need to check the online and bulk_preempt lists again, now
		 * that we are sure no one is messing with them.  If we're
		 * WAITING, we can skip these (or assert they are empty!). */
		vc = TAILQ_FIRST(&p->online_vcs);
		if (vc) {
			/* there's an online vcore, so just alert it (we know it
			 * isn't going anywhere), and return */
			spam_vcore(p, vcore2vcoreid(p, vc), ev_msg, ev_flags);
			spin_unlock(&p->proc_lock);
			return;
		}
		vc = TAILQ_FIRST(&p->bulk_preempted_vcs);
		if (vc) {
			/* the process is bulk preempted, similar deal to above
			 */
			spam_vcore(p, vcore2vcoreid(p, vc), ev_msg, ev_flags);
			spin_unlock(&p->proc_lock);
			return;
		}
	}
	/* At this point, we're sure all vcores are yielded, though we might not
	 * be WAITING.  Post to the first on the inactive list (which is the one
	 * that will definitely be woken up) */
	vc = TAILQ_FIRST(&p->inactive_vcs);
	assert(vc);
	spam_vcore(p, vcore2vcoreid(p, vc), ev_msg, ev_flags);
	/* Set the vcore's alertable flag, to short circuit our last ditch
	 * effort above */
	set_vcore_msgable(vcore2vcoreid(p, vc));
	/* The first event to catch the process with no online/bp vcores will
	 * need to wake it up, which is handled elsewhere if they requested
	 * EVENT_WAKEUP.  We could be RUNNABLE_M here if another event already
	 * woke us and we didn't get lucky with the penultimate fallback. */
	spin_unlock(&p->proc_lock);
}

/* Helper: sends an indirection event for an ev_q, preferring vcoreid */
static void send_indir(struct proc *p, struct event_queue *ev_q,
                       uint32_t vcoreid)
{
	struct event_msg local_msg = {0};
	/* If an alert is already pending and they don't want repeats, just
	 * return.  One of the few uses of NOTHROTTLE will be for preempt_msg
	 * ev_qs.  Ex: an INDIR was already sent to the preempted vcore, then
	 * alert throttling would stop another vcore from getting the message
	 * about the original vcore. */
	if (!(ev_q->ev_flags & EVENT_NOTHROTTLE) && (ev_q->ev_alert_pending))
		return;
	/* We'll eventually get an INDIR through, so don't send any more til
	 * userspace toggles this.  Regardless of other writers to this flag, we
	 * eventually send an alert that causes userspace to turn throttling off
	 * again (before handling all of the ev_q's events).
	 *
	 * This will also squelch IPIs, since there's no reason to send the IPI
	 * if the INDIR is still un-acknowledged.  The vcore is either in vcore
	 * context, attempting to deal with the INDIR, or offline.  This
	 * statement is probably true. */
	ev_q->ev_alert_pending = TRUE;
	wmb();	/* force this write to happen before any event writes */
	local_msg.ev_type = EV_EVENT;
	local_msg.ev_arg3 = ev_q;
	/* If we're not spamming indirs, just send and be done with it.
	 *
	 * It's possible that the user does not want to poll their evq and wants
	 * an INDIR, but also doesn't care about sleeping or otherwise not
	 * getting the message right away.  The INDIR could sit in the VCPD of a
	 * vcore that doesn't run for a while.  Perhaps if the app always made
	 * sure VC 0 was on when it was running at all, and sent the INDIR
	 * there.  Or there was a per-vc evq that only needed to be handled when
	 * the VC turned on.  This gets at another aspect of INDIRs, other than
	 * it's need for "only once" operation: maybe the mbox type isn't a UCQ
	 * (like the VCPD mboxes). */
	if (!(ev_q->ev_flags & EVENT_SPAM_INDIR)) {
		spam_vcore(p, vcoreid, &local_msg, ev_q->ev_flags);
		return;
	}
	/* At this point, we actually want to send and spam an INDIR.
	 * This will guarantee the message makes it to some vcore. */
	spam_public_msg(p, &local_msg, vcoreid, ev_q->ev_flags);
}

/* Send an event to ev_q, based on the parameters in ev_q's flag.  We don't
 * accept null ev_qs, since the caller ought to be checking before bothering to
 * make a msg and send it to the event_q.  Vcoreid is who the kernel thinks the
 * message ought to go to (for IPIs).  Appropriate for things like
 * EV_PREEMPT_PENDING, where we tell the affected vcore.  To have the message go
 * where the kernel suggests, set EVENT_VCORE_APPRO(priate). */
void send_event(struct proc *p, struct event_queue *ev_q, struct event_msg *msg,
                uint32_t vcoreid)
{
	uintptr_t old_proc;
	struct event_mbox *ev_mbox = 0;

	assert(!in_irq_ctx(&per_cpu_info[core_id()]));
	assert(p);
	if (proc_is_dying(p))
		return;
	printd("[kernel] sending msg to proc %p, ev_q %p\n", p, ev_q);
	assert(is_user_rwaddr(ev_q, sizeof(struct event_queue)));
	/* ev_q is a user pointer, so we need to make sure we're in the right
	 * address space */
	old_proc = switch_to(p);
	/* Get the vcoreid that we'll message (if appropriate).  For INDIR and
	 * SPAMMING, this is the first choice of a vcore, but other vcores might
	 * get it.  Common case is !APPRO and !ROUNDROBIN.  Note we are
	 * clobbering the vcoreid parameter. */
	if (!(ev_q->ev_flags & EVENT_VCORE_APPRO))
		vcoreid = ev_q->ev_vcore;	/* use the ev_q's vcoreid */
	/* Note that RR overwrites APPRO */
	if (ev_q->ev_flags & EVENT_ROUNDROBIN) {
		/* Pick a vcore, round-robin style.  Assuming ev_vcore was the
		 * previous one used.  Note that round-robin overrides the
		 * passed-in vcoreid.  Also note this may be 'wrong' if
		 * num_vcores changes. */
		vcoreid = (ev_q->ev_vcore + 1) % p->procinfo->num_vcores;
		ev_q->ev_vcore = vcoreid;
	}
	if (!vcoreid_is_safe(vcoreid)) {
		/* Ought to kill them, just warn for now */
		printk("[kernel] Vcoreid %d unsafe! (too big?)\n", vcoreid);
		goto out;
	}
	/* If we're a SPAM_PUBLIC, they just want us to spam the message.  Note
	 * we don't care about the mbox, since it'll go to VCPD public mboxes,
	 * and we'll prefer to send it to whatever vcoreid we determined at this
	 * point (via APPRO or whatever). */
	if (ev_q->ev_flags & EVENT_SPAM_PUBLIC) {
		spam_public_msg(p, msg, vcoreid, ev_q->ev_flags);
		goto wakeup;
	}
	/* We aren't spamming and we know the default vcore, and now we need to
	 * figure out which mbox to use.  If they provided an mbox, we'll use
	 * it.  If not, we'll use a VCPD mbox (public or private, depending on
	 * the flags). */
	ev_mbox = ev_q->ev_mbox;
	if (!ev_mbox)
		ev_mbox = get_vcpd_mbox(vcoreid, ev_q->ev_flags);
	/* At this point, we ought to have the right mbox to send the msg to,
	 * and which vcore to alert (IPI/INDIR) (if applicable).  The mbox could
	 * be the vcore's vcpd ev_mbox. */
	if (!ev_mbox) {
		/* This shouldn't happen any more, this is more for sanity's
		 * sake */
		warn("[kernel] ought to have an mbox by now!");
		goto out;
	}
	/* Even if we're using an mbox in procdata (VCPD), we want a user
	 * pointer */
	if (!is_user_rwaddr(ev_mbox, sizeof(struct event_mbox))) {
		/* Ought to kill them, just warn for now */
		printk("[kernel] Illegal addr for ev_mbox\n");
		goto out;
	}
	post_ev_msg(p, ev_mbox, msg, ev_q->ev_flags);
	wmb();	/* ensure ev_msg write is before alerting the vcore */
	/* Prod/alert a vcore with an IPI or INDIR, if desired.  INDIR will also
	 * call try_notify (IPI) later */
	if (ev_q->ev_flags & EVENT_INDIR) {
		send_indir(p, ev_q, vcoreid);
	} else {
		/* they may want an IPI despite not wanting an INDIR */
		try_notify(p, vcoreid, ev_q->ev_flags);
	}
wakeup:
	if ((ev_q->ev_flags & EVENT_WAKEUP) && (p->state == PROC_WAITING))
		proc_wakeup(p);
	/* Fall through */
out:
	/* Return to the old address space. */
	switch_back(p, old_proc);
}

/* Send an event for the kernel event ev_num.  These are the "one sided" kernel
 * initiated events, that require a lookup of the ev_q in procdata.  This is
 * roughly equivalent to the old "proc_notify()" */
void send_kernel_event(struct proc *p, struct event_msg *msg, uint32_t vcoreid)
{
	uint16_t ev_num = msg->ev_type;
	assert(ev_num < MAX_NR_EVENT);		/* events start at 0 */
	struct event_queue *ev_q = p->procdata->kernel_evts[ev_num];
	/* linux would put a rmb_depends() here too, i think. */
	if (ev_q)
		send_event(p, ev_q, msg, vcoreid);
}

/* Writes the msg to the vcpd mbox of the vcore.  If you want the private mbox,
 * send in the ev_flag EVENT_VCORE_PRIVATE.  If not, the message could
 * be received by other vcores if the given vcore is offline/preempted/etc.
 * Whatever other flags you pass in will get sent to post_ev_msg.  Currently,
 * the only one that will get looked at is NO_MSG (set a bit).
 *
 * This needs to load current (switch_to), but doesn't need to care about what
 * the process wants.  Note this isn't commonly used - just the monitor and
 * sys_self_notify(). */
void post_vcore_event(struct proc *p, struct event_msg *msg, uint32_t vcoreid,
                      int ev_flags)
{
	/* Need to set p as current to post the event */
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	uintptr_t old_proc = switch_to(p);

	/* *ev_mbox is the user address of the vcpd mbox */
	post_vc_msg(p, vcoreid, get_vcpd_mbox(vcoreid, ev_flags), msg, ev_flags);
	switch_back(p, old_proc);
}

/* Attempts to send a posix signal to the process.  If they do not have an ev_q
 * registered for EV_POSIX_SIGNAL, then nothing will happen. */
void send_posix_signal(struct proc *p, int sig_nr)
{
	struct event_msg local_msg = {0};
	local_msg.ev_type = EV_POSIX_SIGNAL;
	local_msg.ev_arg1 = sig_nr;
	send_kernel_event(p, &local_msg, 0);
}
