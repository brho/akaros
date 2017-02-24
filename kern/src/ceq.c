/* Copyright (c) 2015 Google Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Coalescing Event Queue: encapuslates the essence of epoll/kqueue in shared
 * memory: a dense array of sticky status bits.
 *
 * Kernel side (producer)
 *
 * All of the printks are just us helping the user debug their CEQs. */

#include <ceq.h>
#include <process.h>
#include <stdio.h>
#include <umem.h>

static void error_addr(struct ceq *ceq, struct proc *p, void *addr)
{
	printk("[kernel] Invalid ceq (%p) bad addr %p for proc %d\n", ceq,
	       addr, p->pid);
}

static void ceq_update_max_event(struct ceq *ceq, unsigned int new_max)
{
	unsigned int old_max;

	do {
		old_max = atomic_read(&ceq->max_event_ever);
		if (new_max <= old_max)
			return;
	} while (!atomic_cas(&ceq->max_event_ever, old_max, new_max));
}

void send_ceq_msg(struct ceq *ceq, struct proc *p, struct event_msg *msg)
{
	struct ceq_event *ceq_ev;
	int32_t *ring_slot;
	unsigned long my_slot;
	int loops = 0;
	#define NR_RING_TRIES 10

	/* should have been checked by the kernel func that called us */
	assert(is_user_rwaddr(ceq, sizeof(struct ceq)));
	if (msg->ev_type >= ceq->nr_events) {
		printk("[kernel] CEQ %p too small.  Wanted %d, had %d\n", ceq,
		       msg->ev_type, ceq->nr_events);
		return;
	}
	ceq_update_max_event(ceq, msg->ev_type);
	/* ACCESS_ONCE, prevent the compiler from rereading ceq->events later, and
	 * possibly getting a new, illegal version after our check */
	ceq_ev = &(ACCESS_ONCE(ceq->events))[msg->ev_type];
	if (!is_user_rwaddr(ceq_ev, sizeof(struct ceq_event))) {
		error_addr(ceq, p, ceq);
		return;
	}
	/* ideally, we'd like the blob to be posted after the coal, so that the
	 * 'reason' for the blob is present when the blob is.  but we can't
	 * guarantee that.  after we write the coal, the cons could consume that.
	 * then the next time it looks at us, it could just see the blob - so
	 * there's no good way to keep them together.  the user will just have to
	 * deal with it.  in that case, we might as well do it first, to utilize the
	 * atomic ops's memory barrier. */
	ceq_ev->blob_data = (uint64_t)msg->ev_arg3;
	switch (ceq->operation) {
		case (CEQ_OR):
			atomic_or(&ceq_ev->coalesce, msg->ev_arg2);
			break;
		case (CEQ_ADD):
			atomic_add(&ceq_ev->coalesce, msg->ev_arg2);
			break;
		default:
			printk("[kernel] CEQ %p invalid op %d\n", ceq, ceq->operation);
			return;
	}
	/* write before checking if we need to post (covered by the atomic) */
	if (ceq_ev->idx_posted) {
		/* our entry was updated and posted was still set: we know the consumer
		 * will still check it, so we can safely leave.  If we ever have exit
		 * codes or something from send_*_msg, then we can tell the kernel to
		 * not bother with INDIRS/IPIs/etc.  This is unnecessary now since
		 * INDIRs are throttled */
		return;
	}
	/* at this point, we need to make sure the cons looks at our entry.  it may
	 * have already done so while we were mucking around, but 'poking' them to
	 * look again can't hurt */
	ceq_ev->idx_posted = TRUE;
	/* idx_posted write happens before the writes posting it.  the following
	 * atomic provides the cpu mb() */
	cmb();
	/* I considered checking the buffer for full-ness or the ceq overflow here.
	 * Those would be reads, which would require a wrmb() right above for every
	 * ring post, all for something we check for later anyways and for something
	 * that should be rare.  In return, when we are overflowed, which should be
	 * rare if the user sizes their ring buffer appropriately, we go through a
	 * little more hassle below. */
	/* I tried doing this with fetch_and_add to avoid the while loop and picking
	 * a number of times to try.  The trick is that you need to back out, and
	 * could have multiple producers working on the same slot.  Although the
	 * overflow makes it okay for the producers idxes to be clobbered, it's not
	 * okay to have two producers on the same slot, since there'd only be one
	 * consumer.  Theoretically, you could have a producer delayed a long time
	 * that just clobbers an index at some point in the future, or leaves an
	 * index in the non-init state (-1).  It's a mess. */
	do {
		cmb();	/* reread the indices */
		my_slot = atomic_read(&ceq->prod_idx);
		if (__ring_full(ceq->ring_sz, my_slot,
		                atomic_read(&ceq->cons_pub_idx))) {
			ceq->ring_overflowed = TRUE;
			return;
		}
		if (loops++ == NR_RING_TRIES) {
			ceq->ring_overflowed = TRUE;
			return;
		}
	} while (!atomic_cas(&ceq->prod_idx, my_slot, my_slot + 1));
	/* ring_slot is a user pointer, calculated by ring, my_slot, and sz */
	ring_slot = &(ACCESS_ONCE(ceq->ring))[my_slot & (ceq->ring_sz - 1)];
	if (!is_user_rwaddr(ring_slot, sizeof(int32_t))) {
		/* This is a serious user error.  We're just bailing out, and any
		 * consumers might be spinning waiting on us to produce.  Probably not
		 * though, since the ring slot is bad memory. */
		error_addr(ceq, p, ring_slot);
		return;
	}
	/* At this point, we have a valid slot */
	*ring_slot = msg->ev_type;
}

void ceq_dumper(int pid, struct event_queue *ev_q)
{
	struct proc *p;
	uintptr_t switch_state;
	struct ceq *ceq;

	p = pid2proc(pid);
	if (!p) {
		printk("No such proc %d\n", pid);
		return;
	}
	switch_state = switch_to(p);
	if (ev_q->ev_mbox->type != EV_MBOX_CEQ) {
		printk("Not a CEQ evq (type %d)\n", ev_q->ev_mbox->type);
		goto out;
	}
	ceq = &ev_q->ev_mbox->ceq;
	printk("CEQ %p\n---------------\n"
	       "\tevents ptr %p\n"
	       "\tnr_events %d\n"
	       "\tlast_recovered %d\n"
	       "\tmax_event_ever %ld\n"
	       "\tring %p\n"
	       "\tring_sz %d\n"
	       "\toperation %d\n"
	       "\tring_overflowed %d\n"
	       "\toverflow_recovery %d\n"
	       "\tprod_idx %lu\n"
	       "\tcons_pub_idx %lu\n"
	       "\tcons_pvt_idx %lu\n"
	       "\n",
		   ceq,
	       ceq->events,
	       ceq->nr_events,
	       ceq->last_recovered,
	       atomic_read(&ceq->max_event_ever),
	       ceq->ring,
	       ceq->ring_sz,
	       ceq->operation,
	       ceq->ring_overflowed,
	       ceq->overflow_recovery,
	       atomic_read(&ceq->prod_idx),
	       atomic_read(&ceq->cons_pub_idx),
	       atomic_read(&ceq->cons_pvt_idx));
	for (int i = 0; i < atomic_read(&ceq->max_event_ever) + 1; i++)
		printk("\tEvent %3d, coal %p, blob %p, idx_posted %d, user %p\n", i,
		       atomic_read(&ceq->events[i].coalesce),
		       ceq->events[i].blob_data,
		       ceq->events[i].idx_posted,
		       ceq->events[i].user_data);
out:
	switch_back(p, switch_state);
	proc_decref(p);
}
