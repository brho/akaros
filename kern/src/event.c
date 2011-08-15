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
static bool can_alert_vcore(struct proc *p, uint32_t vcoreid)
{
	struct preempt_data *vcpd = &__procdata.vcore_preempt_data[vcoreid];
	return vcpd->can_rcv_msg;
}

/* Scans the vcoremap, looking for an alertable vcore (returing that vcoreid).
 * If this fails, it's userspace's fault, so we'll complain loudly.  
 *
 * It is possible for a vcore to yield and toggle this flag off before we post
 * the indir, which is why we have that loop in alert_vcore().
 *
 * Note this checks procdata via the user pointer. */
uint32_t find_alertable_vcore(struct proc *p, uint32_t start_loc)
{
	struct procinfo *pi = p->procinfo;
	for (uint32_t i = start_loc; i < pi->max_vcores; i++) {
		if (can_alert_vcore(p, i)) {
			return i;
		}
	}
	/* if we're here, the program is likely fucked.  buggy at least */
	printk("[kernel] no vcores can recv messages!  (user bug)\n");
	return 0;	/* vcore 0 is the most likely to come back online */
}

/* Helper to send an indir, called from a couple places */
static void send_indir_to_vcore(struct event_queue *ev_q, uint32_t vcoreid)
{
	struct event_msg local_msg = {0};
	local_msg.ev_type = EV_EVENT;
	local_msg.ev_arg3 = ev_q;
	post_ev_msg(get_proc_ev_mbox(vcoreid), &local_msg, 0);
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
 * The plan for dealing with FALLBACK is that we get a good vcoreid (can recv
 * messages), then do the IPI/INDIRs, and then check to make sure the vcore is
 * still good.  If the vcore is no longer available, we find another.  Userspace
 * will make sure to turn off the can_recv_msg flag (and then check for messages
 * again) before yielding.
 *
 * I don't particularly care if the vcore is offline or not for INDIRs.  There
 * is a small window when a vcore is offline but can receive messages AND that
 * another vcore is online.  This would only happen when a vcore doesn't respond
 * to a preemption.  This would NOT happen when the entire process was preempted
 * (which is when I would want to send to the initial offline vcore anyway).  In
 * short, if can_recv is set, I'll send it there, and let userspace handle the
 * rare "unresponsive" preemption.  There are a lot of legit reasons why a vcore
 * would be offline (or preempt_pending) and have can_recv set.
 *
 * IPIs don't matter as much.  We'll send them to the (fallback) vcore, but
 * never send them to an offline vcore.  If we lose a race and try to IPI an
 * offline core, proc_notify can handle it.  I do the checks here to avoid some
 * future pain (for now). */
static uint32_t alert_vcore(struct proc *p, struct event_queue *ev_q,
                            uint32_t vcoreid)
{
	int num_loops = 0;
	/* Don't care about FALLBACK, just send and be done with it */
	if (!ev_q->ev_flags & EVENT_FALLBACK) {
		if (ev_q->ev_flags & EVENT_INDIR)
			send_indir_to_vcore(ev_q, vcoreid);
		/* Don't bother with the IPI if the vcore is offline */
		if ((ev_q->ev_flags & EVENT_IPI) && vcore_is_mapped(p, vcoreid))
			proc_notify(p, vcoreid);
		return vcoreid;
	}
	/* If we're here, we care about FALLBACK.  Loop, trying vcores til we don't
	 * lose the race.  It's a user bug (which we'll comment on in a helper) if
	 * there are no vcores willing to rcv a message. */
	do {
		/* Sanity check.  Should never happen, unless we're buggy */
		if (num_loops++ > MAX_NUM_CPUS)
			warn("Having a hard time finding an online vcore");
		/* Preemptively try to get a 'good' vcoreid.  The vcore might actually
		 * be offline. */
		if (!can_alert_vcore(p, vcoreid)) {
			vcoreid = 0;	/* start the search from 0, more likely to be on */
			vcoreid = find_alertable_vcore(p, vcoreid);
		}
		/* If we're here, we think the vcore can recv the INDIR */
		if (ev_q->ev_flags & EVENT_INDIR)
			send_indir_to_vcore(ev_q, vcoreid);
		/* Only send the IPI if it is also online (optimization) */
		if ((ev_q->ev_flags & EVENT_IPI) && vcore_is_mapped(p, vcoreid))
			proc_notify(p, vcoreid);
		wmb();
		/* If the vcore now can't receive the message, we probably lost the
		 * race, so let's loop and try with another.  Some vcore is getting
		 * spurious messages, but those are not incorrect (just slows things a
		 * bit if we lost the race). */
	} while (!can_alert_vcore(p, vcoreid));
	return vcoreid;
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
	/* TODO: If the whole proc is offline, this is where we can check and make
	 * it runnable (if we want).  Alternatively, we can do this only if they
	 * asked for IPIs or INDIRs. */

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
