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
	struct procdata *pd = (struct procdata*)UDATA;
	return &pd->vcore_preempt_data[vcoreid].ev_mbox;
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
	struct event_mbox *ev_mbox = 0, *vcore_mbox;
	struct event_msg local_msg = {0};
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
	/* Vcore options: IPIs and INDIRs */
	if (ev_q->ev_flags & EVENT_INDIR) {
		vcore_mbox = get_proc_ev_mbox(vcoreid);
		/* Help out userspace, since we can detect this bug:*/
		if (ev_mbox == vcore_mbox)
			printk("[kernel] EVENT_INDIR requested for a VCPD mbox!\n");
		/* Actually post the INDIR */
		local_msg.ev_type = EV_EVENT;
		local_msg.ev_arg3 = ev_q;
		post_ev_msg(vcore_mbox, &local_msg, 0);
	}
	if (ev_q->ev_flags & EVENT_IPI)
		proc_notify(p, vcoreid);
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
