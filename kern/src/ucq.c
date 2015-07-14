/* Copyright (c) 2011 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Kernel side of ucqs. */

#include <ucq.h>
#include <umem.h>
#include <assert.h>
#include <mm.h>
#include <atomic.h>

/* Proc p needs to be current, and you should have checked that ucq is valid
 * memory.  We'll assert it here, to catch any of your bugs.  =) */
void send_ucq_msg(struct ucq *ucq, struct proc *p, struct event_msg *msg)
{
	uintptr_t my_slot = 0;
	struct ucq_page *new_page, *old_page;
	struct msg_container *my_msg;

	assert(is_user_rwaddr(ucq, sizeof(struct ucq)));
	/* So we can try to send ucqs to _Ss before they initialize */
	if (!ucq->ucq_ready) {
		if (__proc_is_mcp(p))
			warn("proc %d is _M with an uninitialized ucq %p\n", p->pid, ucq);
		return;
	}
	/* Bypass fetching/incrementing the counter if we're overflowing, helps
	 * prevent wraparound issues on the counter (only 12 bits of counter) */
	if (ucq->prod_overflow)
		goto grab_lock;
	/* Grab a potential slot */
	my_slot = (uintptr_t)atomic_fetch_and_add(&ucq->prod_idx, 1);
	if (slot_is_good(my_slot))
		goto have_slot;
	/* Warn others to not bother with the fetch_and_add */
	ucq->prod_overflow = TRUE;
	/* Sanity check */
	if (PGOFF(my_slot) > 3000)
		warn("Abnormally high counter, there's probably something wrong!");
grab_lock:
	/* Lock, for this proc/ucq.  Using an irqsave, since we may want to send ucq
	 * messages from irq context. */
	hash_lock_irqsave(p->ucq_hashlock, (long)ucq);
	/* Grab a potential slot (again, preventing another DoS) */
	my_slot = (uintptr_t)atomic_fetch_and_add(&ucq->prod_idx, 1);
	if (slot_is_good(my_slot))
		goto unlock_lock;
	/* Check to make sure the old_page was good before we do anything too
	 * intense (we deref it later).  Bad pages are likely due to
	 * user-malfeasance or neglect.
	 *
	 * The is_user_rwaddr() check on old_page might catch addresses below
	 * MMAP_LOWEST_VA, and we can also handle a PF, but we'll explicitly check
	 * for 0 just to be sure (and it's a likely error). */
	old_page = (struct ucq_page*)PTE_ADDR(my_slot);
	if (!is_user_rwaddr(old_page, PGSIZE) || !old_page)
		goto error_addr_unlock;
	/* Things still aren't fixed, so we need to reset everything */
	/* Try to get the spare page, so we don't have to mmap a new one */
	new_page = (struct ucq_page*)atomic_swap(&ucq->spare_pg, 0);
	if (!new_page) {
		/* Warn if we have a ridiculous amount of pages in the ucq */
		if (atomic_fetch_and_add(&ucq->nr_extra_pgs, 1) > UCQ_WARN_THRESH)
			warn("Over %d pages in ucq %p for pid %d!\n", UCQ_WARN_THRESH,
			     ucq, p->pid);
		/* Giant warning: don't ask for anything other than anonymous memory at
		 * a non-fixed location.  o/w, it may cause a TLB shootdown, which grabs
		 * the proc_lock, and potentially deadlock the system. */
		new_page = (struct ucq_page*)do_mmap(p, 0, PGSIZE,
		                                     PROT_READ | PROT_WRITE,
		                                     MAP_ANON | MAP_POPULATE, 0, 0);
		assert(new_page);
		assert(!PGOFF(new_page));
	} else {
		/* If we're using the user-supplied new_page, we need to check it */
		if (!is_user_rwaddr(new_page, PGSIZE) || PGOFF(new_page))
			goto error_addr_unlock;
	}
	/* Now we have a page.  Lets make sure it's set up properly */
	new_page->header.cons_next_pg = 0;
	new_page->header.nr_cons = 0;
	/* Link the old page to the new one, so consumers know how to follow */
	old_page->header.cons_next_pg = (uintptr_t)new_page;
	/* Set the prod_idx counter to 1 (and the new_page), reserving the first
	 * slot (number '0') for us (reservation prevents DoS). */
	my_slot = (uintptr_t)new_page;
	atomic_set(&ucq->prod_idx, my_slot + 1);
	/* Fallthrough to clear overflow and unlock */
unlock_lock:
	/* Clear the overflow, so new producers will try to get a slot */
	ucq->prod_overflow = FALSE;
	/* At this point, any normal (non-locking) producers can succeed in getting
	 * a slot.  The ones that failed earlier will fight for the lock, then
	 * quickly proceed when they get a good slot */
	hash_unlock_irqsave(p->ucq_hashlock, (long)ucq);
	/* Fall through to having a slot */
have_slot:
	/* Sanity check on our slot. */
	assert(slot_is_good(my_slot));
	/* Convert slot to actual msg_container.  Note we never actually deref
	 * my_slot here (o/w we'd need a rw_addr check). */
	my_msg = slot2msg(my_slot);
	/* Make sure our msg is user RW */
	if (!is_user_rwaddr(my_msg, sizeof(struct msg_container)))
		goto error_addr;
	/* Finally write the message */
	my_msg->ev_msg = *msg;
	wmb();
	/* Now that the write is done, signal to the consumer that they can consume
	 * our message (they could have been spinning on it) */
	my_msg->ready = TRUE;
	return;
error_addr_unlock:
	/* Had a bad addr while holding the lock.  This is a bit more serious */
	warn("Bad addr in ucq page management!");
	ucq->prod_overflow = FALSE;
	hash_unlock_irqsave(p->ucq_hashlock, (long)ucq);
	/* Fall-through to normal error out */
error_addr:
	warn("Invalid user address, not sending a message");
	/* TODO: consider killing the process here.  For now, just catch it.  For
	 * some cases, we have a slot that we never fill in, though if we had a bad
	 * addr, none of this will work out and the kernel just needs to protect
	 * itself. */
	return;
}

/* Debugging */
#include <smp.h>
#include <pmap.h>

/* Prints the status and up to 25 of the previous messages for the UCQ. */
void print_ucq(struct proc *p, struct ucq *ucq)
{
	struct ucq_page *ucq_pg;
	struct proc *old_proc = switch_to(p);

	printk("UCQ %p\n", ucq);
	printk("prod_idx: %p, cons_idx: %p\n", atomic_read(&ucq->prod_idx),
	       atomic_read(&ucq->cons_idx));
	printk("spare_pg: %p, nr_extra_pgs: %d\n", atomic_read(&ucq->spare_pg),
	       atomic_read(&ucq->nr_extra_pgs));
	printk("prod_overflow: %d\n", ucq->prod_overflow);
	/* Try to see our previous ucqs */
	for (int i = atomic_read(&ucq->prod_idx), count = 0;
	     slot_is_good(i), count < 25;  i--, count++) {
		/* only attempt to print messages on the same page */
		if (PTE_ADDR(i) != PTE_ADDR(atomic_read(&ucq->prod_idx)))
			break;
		printk("Prod idx %p message ready is %p\n", i, slot2msg(i)->ready);
	}
	/* look at the chain, starting from cons_idx */
	ucq_pg = (struct ucq_page*)PTE_ADDR(atomic_read(&ucq->cons_idx));
	for (int i = 0; i < 10 && ucq_pg; i++) {
		printk("#%02d: Cons page: %p, nr_cons: %d, next page: %p\n", i,
		       ucq_pg, ucq_pg->header.nr_cons, ucq_pg->header.cons_next_pg);
		ucq_pg = (struct ucq_page*)(ucq_pg->header.cons_next_pg);
	}
	switch_back(p, old_proc);
}
