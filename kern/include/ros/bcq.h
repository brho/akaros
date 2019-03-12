/* Copyright (c) 2010 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Multi-producer, multi-consumer queues.  Designed initially for an untrusted
 * consumer.
 */

#pragma once

#include <ros/common.h>
#include <ros/bcq_struct.h>
#include <string.h>

/* Pain in the ass includes.  Glibc has an atomic.h, and eventually userspace
 * will have to deal with the clobbering. */
#ifdef ROS_KERNEL
#include <atomic.h>
/* dequeue uses relax_vc, which is user-only.  Some kernel tests call dequeue.*/
#define cpu_relax_vc(x) cpu_relax()
#else
#include <parlib/arch/atomic.h>
#include <parlib/vcore.h>
#endif /* ROS_KERNEL */

/* Bounded Concurrent Queues, untrusted consumer
 *
 * This is a producer/consumer circular buffer, in which the producer never
 * blocks and does not need to trust the data structure (which is writable by
 * the consumer).
 *
 * A producer enqueues an item, based on the indexes of the producer and
 * consumer.  Enqueue cannot block, but can fail if the queue is full or if it
 * fails to enqueue after a certain amount of tries.
 *
 * prod_idx:     the next item to be produced
 * cons_pvt_idx: the next item a consumer can claim
 * cons_pub_idx: the last item (farthest left / oldest) that hasn't been
 *               consumed/made ready to be clobbered by the producer (it is
 *               what the consumer produces).  Once all are clear, this will be
 *               the same as the prod_idx.
 *
 * The number of free slots in the buffer is: BufSize - (prod_idx - cons_pub)
 * The power-of-two nature of the number of elements makes this work when it
 * wraps around, just like with Xen.  Check it yourself with a counter of 8 and
 * bufsizes of 8 and 4.
 *
 *
 * General plan:
 *
 * Producers compete among themselves, using the prod_idx, to get a free spot.
 * Once they have a spot, they fill in the item, and then toggle the "ready for
 * consumption" bool for a client.  If it cannot find one after a number of
 * tries, it simply fails (could be a DoS from the client).
 *
 * Consumers fight with their private index, which they use to determine who is
 * consuming which item.  If there is an unconsumed item, they try to advance
 * the pvt counter.  If they succeed, they can consume the item.  The item
 * might not be there yet, so they must spin until it is there.  Then, the
 * consumer copies the item out, and clears the bool (rdy_for_cons).
 *
 * At this point, the consumer needs to make sure the pub_idx is advanced
 * enough so the producer knows the item is free.  If pub_idx was their item,
 * they move it forward to the next item.  If it is not, currently, they spin
 * and wait until the previous consumer finishes, and then move it forward.
 * This isn't ideal, and we can deal with this in the future.
 *
 * Enqueue will enqueue the item pointed to by elem.  Dequeue will write an
 * item into the memory pointed to by elem.
 *
 * The number of items must be a power of two.  In the future, we'll probably
 * use Xen's rounding macros.  Not using powers of two is a pain, esp with mods
 * of negative numbers.
 *
 * Here's how to use it:
 *
 * DEFINE_BCQ_TYPES(my_name, my_type, my_size);
 * struct my_name_bcq some_bcq;
 * bcq_init(&some_bcq, my_type, my_size);
 *
 * bcq_enqueue(&some_bcq, &some_my_type, my_size, num_fails_okay);
 * bcq_dequeue(&some_bcq, &some_my_type, my_size);
 *
 * They both return 0 on success, or some error code on failure.
 *
 * TODO later:
 * Automatically round up.
 *
 * Watch out for ABA.  Could use ctrs in the top of the indexes.  Not really an
 * issue, since that would be a wraparound.
 *
 * Consumers could also just set their bit, and have whoever has the pub_idx
 * right before them be the one to advance it all the way up.
 *
 * Using uint32_t for now, since that's the comp_and_swap we have.  We'll
 * probably get other sizes once we're sure we like the current one.  */

#if 0 // Defined in the included header

struct bcq_header {
	uint32_t prod_idx;		/* next to be produced in */
	uint32_t cons_pub_idx;	/* last completely consumed */
	uint32_t cons_pvt_idx;	/* last a consumer has dibs on */
};

// This is there too:
#define DEFINE_BCQ_TYPES(__name, __elem_t, __num_elems)

#endif

/* Functions */
#define bcq_init(_bcq, _ele_type, _num_elems)                                  \
({                                                                             \
	memset((_bcq), 0, sizeof(*(_bcq)));                                    \
	assert((_num_elems) == ROUNDUPPWR2(_num_elems));                       \
})

/* Num empty buffer slots in the BCQ */
#define BCQ_FREE_SLOTS(_p, _cp, _ne) ((_ne) - ((_p) - (_cp)))

/* Really empty */
#define BCQ_EMPTY(_p, _cp, _ne) ((_ne) == BCQ_FREE_SLOTS(_p, _cp, _ne))

/* All work claimed by a consumer */
#define BCQ_NO_WORK(_p, _cpv) ((_p) == (_cpv))

/* Buffer full */
#define BCQ_FULL(_p, _cp, _ne) (0 == BCQ_FREE_SLOTS(_p, _cp, _ne))

/* Figure out the slot you want, bail if it's full, or if you failed too many
 * times, CAS to set the new prod.  Fill yours in, toggle the bool.  Sorry, the
 * macro got a bit ugly, esp with the __retval hackery. */
#define bcq_enqueue(_bcq, _elem, _num_elems, _num_fail)                        \
({                                                                             \
	uint32_t __prod, __new_prod, __cons_pub, __failctr = 0;                \
	int __retval = 0;                                                      \
	do {                                                                   \
		cmb();                                                         \
		if (((_num_fail)) && (__failctr++ >= (_num_fail))) {           \
			__retval = -EFAIL;                                     \
			break;                                                 \
		}                                                              \
		__prod = (_bcq)->hdr.prod_idx;                                 \
		__cons_pub = (_bcq)->hdr.cons_pub_idx;                         \
		if (BCQ_FULL(__prod, __cons_pub, (_num_elems))) {              \
			__retval = -EBUSY;                                     \
			break;                                                 \
		}                                                              \
		__new_prod = __prod + 1;                                       \
	} while (!atomic_cas_u32(&(_bcq)->hdr.prod_idx, __prod, __new_prod));  \
	if (!__retval) {                                                       \
		/* from here out, __prod is the local __prod that we won */    \
		(_bcq)->wraps[__prod & ((_num_elems)-1)].elem = *(_elem);      \
		wmb();                                                         \
		(_bcq)->wraps[__prod & ((_num_elems)-1)].rdy_for_cons = TRUE;  \
	}                                                                      \
	__retval;                                                              \
})

/* Similar to enqueue, spin afterwards til cons_pub is our element, then
 * advance it. */
#define bcq_dequeue(_bcq, _elem, _num_elems)                                   \
({                                                                             \
	uint32_t __prod, __cons_pvt, __new_cons_pvt, __cons_pub;               \
	int __retval = 0;                                                      \
	do {                                                                   \
		cmb();                                                         \
		__prod = (_bcq)->hdr.prod_idx;                                 \
		__cons_pvt = (_bcq)->hdr.cons_pvt_idx;                         \
		if (BCQ_NO_WORK(__prod, __cons_pvt)) {                         \
			__retval = -EBUSY;                                     \
			break;                                                 \
		}                                                              \
		__new_cons_pvt = (__cons_pvt + 1);                             \
	} while (!atomic_cas_u32(&(_bcq)->hdr.cons_pvt_idx, __cons_pvt,        \
	                           __new_cons_pvt));                           \
	if (!__retval) {                                                       \
		/* from here out, __cons_pvt is the local __cons_pvt that we */\
		/* won.  wait for the producer to finish copying it in */      \
		while (!(_bcq)->wraps[__cons_pvt & ((_num_elems)-1)].rdy_for_cons) \
			cpu_relax();                                           \
		*(_elem) = (_bcq)->wraps[__cons_pvt & ((_num_elems)-1)].elem;  \
		(_bcq)->wraps[__cons_pvt & ((_num_elems)-1)].rdy_for_cons = FALSE; \
		/* wait til we're the cons_pub, then advance it by one */      \
		while ((_bcq)->hdr.cons_pub_idx != __cons_pvt)                 \
			cpu_relax_vc(vcore_id());                              \
		(_bcq)->hdr.cons_pub_idx = __cons_pvt + 1;                     \
	}                                                                      \
	__retval;                                                              \
})

/* Checks of a bcq is empty (meaning no work), instead of trying to dequeue */
#define bcq_empty(_bcq)                                                        \
	BCQ_NO_WORK((_bcq)->hdr.prod_idx, (_bcq)->hdr.cons_pvt_idx)

#define bcq_nr_full(_bcq)                                                      \
	((_bcq)->hdr.prod_idx - (_bcq)->hdr.cons_pub_idx)
