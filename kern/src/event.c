/* Copyright (c) 2011 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Kernel utility functions for sending events and notifications (IPIs) to
 * processes. */

#include <ucq.h>
#include <bitmask.h>
#include <event.h>
#include <atomic.h>
#include <process.h>
#include <smp.h>
#include <umem.h>
#include <stdio.h>
#include <assert.h>
#include <pmap.h>

/* Note this returns the user address of the mbox, not the KVA.  You'll need
 * current loaded to access this, and it will work for any process. */
static struct event_mbox *get_proc_ev_mbox(uint32_t vcoreid)
{
	return &__procdata.vcore_preempt_data[vcoreid].ev_mbox;
}

/* Posts a message to the mbox, subject to flags.  Feel free to send 0 for the
 * flags if you don't want to give them the option of EVENT_NOMSG (which is what
 * we do when sending an indirection event).  Make sure that if mbox is a user
 * pointer, that you've checked it *and* have that processes address space
 * loaded.  This can get called with a KVA for mbox. */
static void post_ev_msg(struct event_mbox *mbox, struct event_msg *msg,
                        int ev_flags)
{
	struct proc *p = current;
	printd("[kernel] Sending event type %d to mbox %08p\n", msg->ev_type, mbox);
	/* Sanity check */
	assert(p);
	/* If they just want a bit (NOMSG), just set the bit */
	if (ev_flags & EVENT_NOMSG) {
		SET_BITMASK_BIT_ATOMIC(mbox->ev_bitmap, msg->ev_type);
	} else {
		send_ucq_msg(&mbox->ev_msgs, p, msg);
	}
}

/* Can we alert the vcore?  (Will it check its messages).  Note this checks
 * procdata via the user pointer. */
static bool can_alert_vcore(uint32_t vcoreid)
{
	struct preempt_data *vcpd = &__procdata.vcore_preempt_data[vcoreid];
	return vcpd->can_rcv_msg;
}

/* Says a vcore can be alerted.  Only call this once you are sure this is true
 * (holding the proc_lock, etc. */
static void set_vcore_alertable(uint32_t vcoreid)
{
	struct preempt_data *vcpd = &__procdata.vcore_preempt_data[vcoreid];
	vcpd->can_rcv_msg = TRUE;
}

/* Helper to send an indir, called from a couple places.  Note this uses a
 * userspace address for the VCPD (though not a user's pointer). */
static void send_indir_to_vcore(struct event_queue *ev_q, uint32_t vcoreid)
{
	struct preempt_data *vcpd = &__procdata.vcore_preempt_data[vcoreid];
	struct event_msg local_msg = {0};
	local_msg.ev_type = EV_EVENT;
	local_msg.ev_arg3 = ev_q;
	post_ev_msg(get_proc_ev_mbox(vcoreid), &local_msg, 0);
	/* Set notif pending, so userspace doesn't miss the INDIR while yielding */
	wmb();
	vcpd->notif_pending = TRUE;
}

/* Yet another helper, will post INDIRs and IPI a vcore, based on the needs of
 * an ev_q.  This is called by alert_vcore(), which handles finding the vcores
 * to alert. */
static void __alert_vcore(struct proc *p, struct event_queue *ev_q,
                          uint32_t vcoreid)
{
	if (ev_q->ev_flags & EVENT_INDIR)
		send_indir_to_vcore(ev_q, vcoreid);
	/* Only send the IPI if it is also online (optimization).  There's a race
	 * here, but proc_notify should be able to handle it (perhaps in the
	 * future). TODO: we might need to send regardless of mapping. */
	if ((ev_q->ev_flags & EVENT_IPI) && vcore_is_mapped(p, vcoreid))
		proc_notify(p, vcoreid);
}

/* Attempts to alert a vcore that may or may not have 'can_rcv_msg' set.  If so,
 * we'll post the message and the message will eventually get dealt with (when
 * the vcore runs or when it is preempte-recovered). */
static bool try_alert_vcore(struct proc *p, struct event_queue *ev_q,
                            uint32_t vcoreid)
{
	/* Not sure if we can or not, so check before spamming.  Technically, the
	 * only critical part is that we __alert, then check can_alert. */
	if (can_alert_vcore(vcoreid)) {
		__alert_vcore(p, ev_q, vcoreid);
		cmb();
		if (can_alert_vcore(vcoreid))
			return TRUE;
	}
	return FALSE;
}

/* Helper: will try to alert (INDIR/IPI) a list member (lists of vcores).  We
 * use this on the online and bulk_preempted vcore lists.  If this succeeds in
 * alerting a vcore on the list, it'll return TRUE.  We need to be careful here,
 * since we're reading a list that could be concurrently modified.  The
 * important thing is that we can always fail if we're unsure (such as with
 * lists being temporarily empty).  The caller will be able to deal with it via
 * the ultimate fallback. */
static bool __alert_list_member(struct vcore_tailq *list, struct proc *p,
                                struct event_queue *ev_q)
{
	struct vcore *vc, *vc_first;
	uint32_t vcoreid;
	int loops = 0;
	vc = TAILQ_FIRST(list);
	/* If the list appears empty, we'll bail out (failing) after the loop. */
	while (vc) {
		vcoreid = vcore2vcoreid(p, vc);
		/* post the alert.  Not using the try_alert_vcore() helper since I want
		 * something more customized for the lists. */
		__alert_vcore(p, ev_q, vcoreid);
		cmb();
		/* if they are still alertable after we sent the msg, then they'll get
		 * it before yielding (racing with userspace yield here).  This check is
		 * not as critical as the next one, but will allow us to alert vcores
		 * that happen to concurrently be moved from the active to the
		 * bulk_preempt list. */
		if (can_alert_vcore(vcoreid))
			return TRUE;
		cmb();
		/* As a backup, if they are still the first on the list, then they are
		 * still going to get the message.  For the online list, proc_yield()
		 * will return them to userspace (where they will get the message)
		 * because __alert_vcore() set notif_pending.  For the BP list, they
		 * will either be turned on later, or have a preempt message sent about
		 * their demise.
		 *
		 * We race on list membership (and not exclusively 'can_rcv_msg', so
		 * that when it fails we can get a new vcore to try (or know WHP there
		 * are none). */
		vc_first = TAILQ_FIRST(list);
		if (vc == vc_first)
			return TRUE;
		/* At this point, the list has changed and the vcore we tried yielded,
		 * so we try the *new* list head.  Track loops for sanity reasons. */
		if (loops++ > 10) {
			warn("Too many (%d) attempts to find a vcore, failing!", loops);
			return FALSE;	/* always safe to fail! */
		}
		/* Get set up for your attack run! */
		vc = vc_first;
	}
	return FALSE;
}

/* Helper that alerts a vcore, by IPI and/or INDIR, that it needs to check the
 * ev_q.  Handles FALLBACK and other tricky things.  Returns which vcore was
 * alerted.  The only caller of this is send_event(), and this makes it a little
 * clearer/easier.
 *
 * One of the goals of FALLBACK (and this func) is to allow processes to yield
 * cores without fear of losing messages (INDIR messages, btw (aka, non-vcore
 * business)).
 *
 * We try the desired vcore, using 'can_rcv_msg'.  Failing that, we'll search
 * the online and then the bulk_preempted lists.  These lists serve as a way to
 * find likely alertable vcores.  __alert_list_member() helps us with them,
 * failing if anything seems to go wrong.  At which point we just lock and try
 * to deal with things.  In that scenario, we most likely would need to lock
 * anyway to wake up the process (was WAITING).
 *
 * One tricky thing with sending to the bulk_preempt list is that we may want to
 * send a message about a (bulk) preemption to someone on that list.  This works
 * since a given vcore that was preempted will be removed from that list before
 * we try to send_event() (in theory, there isn't code that can send that event
 * yet).  Someone else will get the event and wake up the preempted vcore. */
static void alert_vcore(struct proc *p, struct event_queue *ev_q,
                        uint32_t vcoreid)
{
	struct vcore *vc;
	/* If an alert is already pending and they don't want repeats, just return.
	 * One of the few uses of NOTHROTTLE will be for preempt_msg ev_qs.  Ex: an
	 * INDIR was already sent to the preempted vcore, then alert throttling
	 * would stop another vcore from getting the message about the original
	 * vcore. */
	if (!(ev_q->ev_flags & EVENT_NOTHROTTLE) && (ev_q->ev_alert_pending))
		return;
	/* We'll eventually get an INDIR through, so don't send any more til
	 * userspace toggles this.  Regardless of other writers to this flag, we
	 * eventually send an alert that causes userspace to turn throttling off
	 * again (before handling all of the ev_q's events).
	 *
	 * This will also squelch IPIs, since there's no reason to send the IPI if
	 * the INDIR is still un-acknowledged.  The vcore is either in vcore
	 * context, attempting to deal with the INDIR, or offline.  This statement
	 * is probably true. */
	if (ev_q->ev_flags & EVENT_INDIR) {
		ev_q->ev_alert_pending = TRUE;
	}
	/* Don't care about FALLBACK, just send and be done with it.  TODO:
	 * considering getting rid of FALLBACK as an option and making it mandatory
	 * when you want an INDIR.  Having trouble thinking of when you'd want an
	 * INDIR but not a FALLBACK. */
	if (!ev_q->ev_flags & EVENT_FALLBACK) {
		if (ev_q->ev_flags & EVENT_INDIR)
			printk("[kernel] INDIR requested without FALLBACK, prob a bug.\n");
		__alert_vcore(p, ev_q, vcoreid);
		return;
	}
	/* If we're here, we care about FALLBACK. First, try posting to the desired
	 * vcore (so long as we don't have to send it to a vcore that will run, like
	 * we do for preempt messages). */
	if (!(ev_q->ev_flags & EVENT_VCORE_MUST_RUN) &&
	   (try_alert_vcore(p, ev_q, vcoreid)))
		return;
	/* If the process is WAITING, let's just jump to the fallback */
	if (p->state == PROC_WAITING)
		goto ultimate_fallback;
	/* If we're here, the desired vcore is unreachable, but the process is
	 * probably RUNNING_M (online_vs) or RUNNABLE_M (bulk preempted or recently
	 * woken up), so we'll need to find another vcore. */
	if (__alert_list_member(&p->online_vcs, p, ev_q))
		return;
	if (__alert_list_member(&p->bulk_preempted_vcs, p, ev_q))
		return;
	/* Last chance, let's check the head of the inactives.  It might be
	 * alertable (the kernel set it earlier due to an event, or it was a
	 * bulk_preempt that didn't restart), and we can avoid grabbing the
	 * proc_lock. */
	vc = TAILQ_FIRST(&p->inactive_vcs);
	if (vc) {	/* might be none in rare circumstances */
		if (try_alert_vcore(p, ev_q, vcore2vcoreid(p, vc))) {
			/* Need to ensure the proc wakes up, but only if it was WAITING.
			 * One way for this to happen is if a normal vcore was preempted
			 * right as another vcore was yielding, and the preempted
			 * message was sent after the last vcore yielded (which caused
			 * us to be WAITING */
			if (p->state == PROC_WAITING) {
				spin_lock(&p->proc_lock);
				__proc_wakeup(p);	/* internally, this double-checks WAITING */
				spin_unlock(&p->proc_lock);
			}
			return;
		}
	}
ultimate_fallback:
	/* At this point, we can't find one.  This could be due to a (hopefully
	 * rare) weird yield/request storm, or more commonly because the lists were
	 * empty and the process is simply WAITING (yielded all of its vcores and is
	 * waiting on an event).  Time for the ultimate fallback: locking.  Note
	 * that when we __alert_vcore(), there is a chance we need to mmap, which
	 * grabs the mm_lock. */
	spin_lock(&p->proc_lock);
	if (p->state != PROC_WAITING) {
		/* We need to check the online and bulk_preempt lists again, now that we are
		 * sure no one is messing with them.  If we're WAITING, we can skip
		 * these (or assert they are empty!). */
		vc = TAILQ_FIRST(&p->online_vcs);
		if (vc) {
			/* there's an online vcore, so just alert it (we know it isn't going
			 * anywhere), and return */
			__alert_vcore(p, ev_q, vcore2vcoreid(p, vc));
			spin_unlock(&p->proc_lock);
			return;
		}
		vc = TAILQ_FIRST(&p->bulk_preempted_vcs);
		if (vc) {
			/* the process is bulk preempted, similar deal to above */
			__alert_vcore(p, ev_q, vcore2vcoreid(p, vc));
			spin_unlock(&p->proc_lock);
			return;
		}
	}
	/* At this point, we're sure all vcores are yielded, though we might not be
	 * WAITING.  Post to the first on the inactive list (which is the one that
	 * will definitely be woken up) */
	vc = TAILQ_FIRST(&p->inactive_vcs);
	assert(vc);
	__alert_vcore(p, ev_q, vcore2vcoreid(p, vc));
	/* Set the vcore's alertable flag, to short circuit our last ditch effort
	 * above */
	set_vcore_alertable(vcore2vcoreid(p, vc));
	/* The first event to catch the process with no online/bp vcores will need
	 * to wake it up.  (We could be RUNNABLE_M here if another event already woke
	 * us.) and we didn't get lucky with the penultimate fallback.
	 * __proc_wakeup() will check for WAITING. */
	__proc_wakeup(p);
	spin_unlock(&p->proc_lock);
	return;
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
	struct proc *old_proc;
	struct event_mbox *ev_mbox = 0;
	assert(p);
	printd("[kernel] sending msg to proc %08p, ev_q %08p\n", p, ev_q);
	if (!ev_q) {
		warn("[kernel] Null ev_q - kernel code should check before sending!");
		return;
	}
	if (!is_user_rwaddr(ev_q, sizeof(struct event_queue))) {
		/* Ought to kill them, just warn for now */
		warn("[kernel] Illegal addr for ev_q");
		return;
	}
	/* ev_q is a user pointer, so we need to make sure we're in the right
	 * address space */
	old_proc = switch_to(p);
	/* Get the mbox and vcoreid */
	/* If we're going with APPRO, we use the kernel's suggested vcore's ev_mbox.
	 * vcoreid is already what the kernel suggests. */
	if (ev_q->ev_flags & EVENT_VCORE_APPRO) {
		ev_mbox = get_proc_ev_mbox(vcoreid);
	} else {	/* common case */
		ev_mbox = ev_q->ev_mbox;
		vcoreid = ev_q->ev_vcore;
	}
	/* Check on the style, which could affect our mbox selection.  Other styles
	 * would go here (or in similar functions we call to).  Important thing is
	 * we come out knowing which vcore to send to in the event of an IPI/INDIR,
	 * and we know what mbox to post to. */
	if (ev_q->ev_flags & EVENT_ROUNDROBIN) {
		/* Pick a vcore, and if we don't have a mbox yet, pick that vcore's
		 * default mbox.  Assuming ev_vcore was the previous one used.  Note
		 * that round-robin overrides the passed-in vcoreid. */
		vcoreid = (ev_q->ev_vcore + 1) % p->procinfo->num_vcores;
		ev_q->ev_vcore = vcoreid;
		/* Note that the style of not having a specific ev_mbox may go away.  I
		 * can't think of legitimate uses of this for now, since things that are
		 * RR probably are non-vcore-business, and thus inappropriate for a VCPD
		 * ev_mbox. */
		if (!ev_mbox)
			ev_mbox = get_proc_ev_mbox(vcoreid);
	}
	/* At this point, we ought to have the right mbox to send the msg to, and
	 * which vcore to send an IPI to (if we send one).  The mbox could be the
	 * vcore's vcpd ev_mbox.  The vcoreid only matters for IPIs and INDIRs. */
	if (!ev_mbox) {
		/* this is a process error */
		warn("[kernel] ought to have an mbox by now!");
		goto out;
	}
	/* Even if we're using an mbox in procdata (VCPD), we want a user pointer */
	if (!is_user_rwaddr(ev_mbox, sizeof(struct event_mbox))) {
		/* Ought to kill them, just warn for now */
		warn("[kernel] Illegal addr for ev_mbox");
		goto out;
	}
	/* We used to support no msgs, but quit being lazy and send a 'msg'.  If the
	 * ev_q is a NOMSG, we won't actually memcpy or anything, it'll just be a
	 * vehicle for sending the ev_type. */
	assert(msg);
	post_ev_msg(ev_mbox, msg, ev_q->ev_flags);
	/* Help out userspace a bit by checking for a potentially confusing bug */
	if ((ev_mbox == get_proc_ev_mbox(vcoreid)) &&
	    (ev_q->ev_flags & EVENT_INDIR))
		printk("[kernel] User-bug: ev_q has an INDIR with a VCPD ev_mbox!\n");
	/* Prod/alert a vcore with an IPI or INDIR, if desired */
	if ((ev_q->ev_flags & (EVENT_IPI | EVENT_INDIR)))
		alert_vcore(p, ev_q, vcoreid);
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
	if (ev_q)
		send_event(p, ev_q, msg, vcoreid);
}

/* Writes the msg to the vcpd/default mbox of the vcore.  Needs to load current,
 * but doesn't need to care about what the process wants.  Note this isn't
 * commonly used - just the monitor and sys_self_notify(). */
void post_vcore_event(struct proc *p, struct event_msg *msg, uint32_t vcoreid)
{
	/* Need to set p as current to post the event */
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	struct proc *old_proc = switch_to(p);
	/* *ev_mbox is the user address of the vcpd mbox */
	post_ev_msg(get_proc_ev_mbox(vcoreid), msg, 0);	/* no chance for a NOMSG */
	switch_back(p, old_proc);
}
