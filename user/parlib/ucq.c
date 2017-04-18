/* Copyright (c) 2011 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Unbounded concurrent queues, user side.  Check k/i/r/ucq.h or the
 * Documentation for more info. */

#include <ros/arch/membar.h>
#include <parlib/arch/atomic.h>
#include <parlib/arch/arch.h>
#include <parlib/ucq.h>
#include <parlib/spinlock.h>
#include <sys/mman.h>
#include <parlib/assert.h>
#include <parlib/stdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <parlib/vcore.h>
#include <parlib/ros_debug.h> /* for printd() */

/* Initializes a ucq.  You pass in addresses of mmaped pages for the main page
 * (prod_idx) and the spare page.  I recommend mmaping a big chunk and breaking
 * it up over a bunch of ucqs, instead of doing a lot of little mmap() calls. */
void ucq_init_raw(struct ucq *ucq, uintptr_t pg1, uintptr_t pg2)
{
	printd("[user] initializing ucq %08p for proc %d\n", ucq, getpid());
	assert(!PGOFF(pg1));
	assert(!PGOFF(pg2));
	/* Prod and cons both start on the first page, slot 0.  When they are equal,
	 * the ucq is empty. */
	atomic_set(&ucq->prod_idx, pg1);
	atomic_set(&ucq->cons_idx, pg1);
	ucq->prod_overflow = FALSE;
	atomic_set(&ucq->nr_extra_pgs, 0);
	atomic_set(&ucq->spare_pg, pg2);
	parlib_static_assert(sizeof(struct spin_pdr_lock) <= sizeof(ucq->u_lock));
	spin_pdr_init((struct spin_pdr_lock*)(&ucq->u_lock));
	ucq->ucq_ready = TRUE;
}

/* Inits a ucq, where you don't have to bother with the memory allocation.  This
 * would be appropriate for one or two UCQs, though if you're allocating in
 * bulk, use the raw version. */
void ucq_init(struct ucq *ucq)
{
	uintptr_t two_pages = (uintptr_t)mmap(0, PGSIZE * 2,
	                                      PROT_WRITE | PROT_READ,
	                                      MAP_POPULATE | MAP_ANONYMOUS, -1, 0);
	assert(two_pages);
	ucq_init_raw(ucq, two_pages, two_pages + PGSIZE);
}

/* Only call this on ucq's made with the simple ucq_init().  And be sure the ucq
 * is no longer in use. */
void ucq_free_pgs(struct ucq *ucq)
{
	uintptr_t pg1 = atomic_read(&ucq->prod_idx);
	uintptr_t pg2 = atomic_read(&ucq->spare_pg);
	assert(pg1 && pg2);
	munmap((void*)pg1, PGSIZE);
	munmap((void*)pg2, PGSIZE);
}

/* Consumer side, returns TRUE on success and fills *msg with the ev_msg.  If
 * the ucq appears empty, it will return FALSE.  Messages may have arrived after
 * we started getting that we do not receive. */
bool get_ucq_msg(struct ucq *ucq, struct event_msg *msg)
{
	uintptr_t my_idx;
	struct ucq_page *old_page, *other_page;
	struct msg_container *my_msg;
	struct spin_pdr_lock *ucq_lock = (struct spin_pdr_lock*)(&ucq->u_lock);

	do {
loop_top:
		cmb();
		my_idx = atomic_read(&ucq->cons_idx);
		/* The ucq is empty if the consumer and producer are on the same 'next'
		 * slot. */
		if (my_idx == atomic_read(&ucq->prod_idx))
			return FALSE;
		/* Is the slot we want good?  If not, we're going to need to try and
		 * move on to the next page.  If it is, we bypass all of this and try to
		 * CAS on us getting my_idx. */
		if (slot_is_good(my_idx))
			goto claim_slot;
		/* Slot is bad, let's try and fix it */
		spin_pdr_lock(ucq_lock);
		/* Reread the idx, in case someone else fixed things up while we
		 * were waiting/fighting for the lock */
		my_idx = atomic_read(&ucq->cons_idx);
		if (slot_is_good(my_idx)) {
			/* Someone else fixed it already, let's just try to get out */
			spin_pdr_unlock(ucq_lock);
			/* Make sure this new slot has a producer (ucq isn't empty) */
			if (my_idx == atomic_read(&ucq->prod_idx))
				return FALSE;
			goto claim_slot;
		}
		/* At this point, the slot is bad, and all other possible consumers are
		 * spinning on the lock.  Time to fix things up: Set the counter to the
		 * next page, and free the old one. */
		/* First, we need to wait and make sure the kernel has posted the next
		 * page.  Worst case, we know that the kernel is working on it, since
		 * prod_idx != cons_idx */
		old_page = (struct ucq_page*)PTE_ADDR(my_idx);
		while (!old_page->header.cons_next_pg)
			cpu_relax();
		/* Now set the counter to the next page */
		assert(!PGOFF(old_page->header.cons_next_pg));
		atomic_set(&ucq->cons_idx, old_page->header.cons_next_pg);
		/* Side note: at this point, any *new* consumers coming in will grab
		 * slots based off the new counter index (cons_idx) */
		/* Now free up the old page.  Need to make sure all other consumers are
		 * done.  We spin til enough are done, like an inverted refcnt. */
		while (atomic_read(&old_page->header.nr_cons) < NR_MSG_PER_PAGE) {
			/* spinning on userspace here, specifically, another vcore and we
			 * don't know who it is.  This will spin a bit, then make sure they
			 * aren't preeempted */
			cpu_relax_any();
		}
		/* Now the page is done.  0 its metadata and give it up. */
		old_page->header.cons_next_pg = 0;
		atomic_set(&old_page->header.nr_cons, 0);
		/* We want to "free" the page.  We'll try and set it as the spare.  If
		 * there is already a spare, we'll free that one. */
		other_page = (struct ucq_page*)atomic_swap(&ucq->spare_pg,
		                                           (long)old_page);
		assert(!PGOFF(other_page));
		if (other_page) {
			munmap(other_page, PGSIZE);
			atomic_dec(&ucq->nr_extra_pgs);
		}
		/* All fixed up, unlock.  Other consumers may lock and check to make
		 * sure things are done. */
		spin_pdr_unlock(ucq_lock);
		/* Now that everything is fixed, try again from the top */
		goto loop_top;
claim_slot:
		cmb();	/* so we can goto claim_slot */
		/* If we're still here, my_idx is good, and we'll try to claim it.  If
		 * we fail, we need to repeat the whole process. */
	} while (!atomic_cas(&ucq->cons_idx, my_idx, my_idx + 1));
	assert(slot_is_good(my_idx));
	/* Now we have a good slot that we can consume */
	my_msg = slot2msg(my_idx);
	/* linux would put an rmb_depends() here */
	/* Wait til the msg is ready (kernel sets this flag) */
	while (!my_msg->ready)
		cpu_relax();
	rmb();	/* order the ready read before the contents */
	/* Copy out */
	*msg = my_msg->ev_msg;
	/* Unset this for the next usage of the container */
	my_msg->ready = FALSE;
	wmb();	/* post the ready write before incrementing */
	/* Increment nr_cons, showing we're done */
	atomic_inc(&((struct ucq_page*)PTE_ADDR(my_idx))->header.nr_cons);
	return TRUE;
}

bool ucq_is_empty(struct ucq *ucq)
{
	/* The ucq is empty if the consumer and producer are on the same 'next'
	 * slot. */
	return (atomic_read(&ucq->cons_idx) == atomic_read(&ucq->prod_idx));
}
