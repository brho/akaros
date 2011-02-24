/* Copyright (c) 2011 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Kernel utility functions for sending events and notifications (IPIs) to
 * processes. */

#include <ros/bcq.h>
#include <bitmask.h>
#include <event.h>
#include <atomic.h>
#include <process.h>
#include <smp.h>
#include <umem.h>
#include <stdio.h>
#include <assert.h>
#include <pmap.h>

/* Note this returns the KVA of the mbox, not the user one. */
static struct event_mbox *get_proc_ev_mbox(struct proc *p, uint32_t vcoreid)
{
	return &p->procdata->vcore_preempt_data[vcoreid].ev_mbox;
}

/* Posts a message to the mbox, subject to flags.  Feel free to send 0 for the
 * flags if you don't want to give them the option of EVENT_NOMSG (which is what
 * we do when sending an indirection event).  Make sure that if mbox is a user
 * pointer, that you've checked it *and* have that processes address space
 * loaded.  This can get called with a KVA for mbox. */
static void post_ev_msg(struct event_mbox *mbox, struct event_msg *msg,
                        int ev_flags)
{
	printd("Sending event type %d\n", msg->ev_type);
	/* Sanity check */
	if (is_user_rwaddr(mbox))
		assert(current);
	/* If they just want a bit (NOMSG), just set the bit */
	if (ev_flags & EVENT_NOMSG) {
		SET_BITMASK_BIT_ATOMIC(mbox->ev_bitmap, msg->ev_type);
	} else {
		/* Enqueue returns 0 on success.  On failure, set a bit. */
		if (bcq_enqueue(&mbox->ev_msgs, msg, NR_BCQ_EVENTS, NR_BCQ_EV_LOOPS)) {
			atomic_inc((atomic_t)&mbox->ev_overflows); // careful here
			SET_BITMASK_BIT_ATOMIC(mbox->ev_bitmap, msg->ev_type);
			/* Catch "lots" of overflows that aren't acknowledged */
			if (mbox->ev_overflows > 10000)
				warn("proc %d has way too many overflows", current->pid);
		}
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
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	struct proc *old_proc = pcpui->cur_proc;	/* uncounted ref */
	struct event_mbox *ev_mbox = 0, *vcore_mbox;
	struct event_msg local_msg = {0};
	assert(p);
	if (!ev_q) {
		warn("[kernel] Null ev_q - kernel code should check before sending!");
		return;
	}
	if (!is_user_rwaddr(ev_q)) {
		/* Ought to kill them, just warn for now */
		warn("[kernel] Illegal addr for ev_q");
		return;
	}
	/* ev_q can be a user pointer (not in procdata), so we need to make sure
	 * we're in the right address space */
	if (old_proc != p) {
		/* Technically, we're storing a ref here, but our current ref on p is
		 * sufficient (so long as we don't decref below) */
		pcpui->cur_proc = p;
		lcr3(p->env_cr3);
	}
	/* Get the mbox and vcoreid */
	/* If we're going with APPRO, we use the kernel's suggested vcore's ev_mbox.
	 * vcoreid is already what the kernel suggests. */
	if (ev_q->ev_flags & EVENT_VCORE_APPRO) {
		ev_mbox = get_proc_ev_mbox(p, vcoreid);
	} else {	/* common case */
		ev_mbox = ev_q->ev_mbox;
		vcoreid = ev_q->ev_vcore;
	}
	/* Check on the style, which could affect our mbox selection.  Other styles
	 * would go here (or in similar functions we call to).  Important thing is
	 * we come out knowing which vcore to send to in the event of an IPI, and we
	 * know what mbox to post to. */
	if (ev_q->ev_flags & EVENT_ROUNDROBIN) {
		/* Pick a vcore, and if we don't have a mbox yet, pick that vcore's
		 * default mbox.  Assuming ev_vcore was the previous one used.  Note
		 * that round-robin overrides the passed-in vcoreid. */
		vcoreid = (ev_q->ev_vcore + 1) % p->procinfo->num_vcores;
		ev_q->ev_vcore = vcoreid;
		if (!ev_mbox)
			ev_mbox = get_proc_ev_mbox(p, vcoreid);
	}
	/* At this point, we ought to have the right mbox to send the msg to, and
	 * which vcore to send an IPI to (if we send one).  The mbox could be the
	 * vcore's vcpd ev_mbox. */
	if (!ev_mbox) {
		/* this is a process error */
		warn("[kernel] ought to have an mbox by now!");
		goto out;
	}
	vcore_mbox = get_proc_ev_mbox(p, vcoreid);
	/* Allows the mbox to be the right vcoreid mbox (a KVA in procdata), or any
	 * other user RW location. */
	if ((ev_mbox != vcore_mbox) && (!is_user_rwaddr(ev_mbox))) {
		/* Ought to kill them, just warn for now */
		warn("[kernel] Illegal addr for ev_mbox");
		goto out;
	}
	/* We used to support no msgs, but quit being lazy and send a msg */
	assert(msg);
	post_ev_msg(ev_mbox, msg, ev_q->ev_flags);
	/* Optional IPIs */
	if (ev_q->ev_flags & EVENT_IPI) {
		/* if the mbox we sent to isn't the default one, we need to send the
		 * vcore an ev_q indirection event */
		if ((ev_mbox != vcore_mbox) && (!uva_is_kva(p, ev_mbox, vcore_mbox))) {
			/* it is tempting to send_kernel_event(), using the ev_q for that
			 * event, but that is inappropriate here, since we are sending to a
			 * specific vcore */
			local_msg.ev_type = EV_EVENT;
			local_msg.ev_arg3 = ev_q;
			post_ev_msg(vcore_mbox, &local_msg, 0);
		}
		proc_notify(p, vcoreid);
	}
out:
	/* Return to the old address space.  We switched to p in the first place if
	 * it wasn't the same as the original current (old_proc). */
	if (old_proc != p) {
		pcpui->cur_proc = old_proc;
		if (old_proc)
			lcr3(old_proc->env_cr3);
		else
			lcr3(boot_cr3);
	}
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

/* Writes the msg to the vcpd/default mbox of the vcore.  Doesn't need to check
 * for current, or care about what the process wants. */
void post_vcore_event(struct proc *p, struct event_msg *msg, uint32_t vcoreid)
{
	struct event_mbox *vcore_mbox;
	/* kernel address of the vcpd mbox */
	vcore_mbox = get_proc_ev_mbox(p, vcoreid);
	post_ev_msg(vcore_mbox, msg, 0);		/* no chance for a NOMSG either */
}
