/*
 * Read-Copy Update definitions shared among RCU implementations.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 * Copyright IBM Corporation, 2011
 *
 * Author: Paul E. McKenney <paulmck@linux.vnet.ibm.com>
 */

#ifndef __LINUX_RCU_H
#define __LINUX_RCU_H

/*
 * Grace-period counter management.
 */

#define RCU_SEQ_CTR_SHIFT	2
#define RCU_SEQ_STATE_MASK	((1 << RCU_SEQ_CTR_SHIFT) - 1)

/*
 * Return the counter portion of a sequence number previously returned
 * by rcu_seq_snap() or rcu_seq_current().
 */
static inline unsigned long rcu_seq_ctr(unsigned long s)
{
	return s >> RCU_SEQ_CTR_SHIFT;
}

/*
 * Return the state portion of a sequence number previously returned
 * by rcu_seq_snap() or rcu_seq_current().
 */
static inline int rcu_seq_state(unsigned long s)
{
	return s & RCU_SEQ_STATE_MASK;
}

/*
 * Set the state portion of the pointed-to sequence number.
 * The caller is responsible for preventing conflicting updates.
 */
static inline void rcu_seq_set_state(unsigned long *sp, int newstate)
{
	warn_on_once(newstate & ~RCU_SEQ_STATE_MASK);
	WRITE_ONCE(*sp, (*sp & ~RCU_SEQ_STATE_MASK) + newstate);
}

/* Adjust sequence number for start of update-side operation. */
static inline void rcu_seq_start(unsigned long *sp)
{
	WRITE_ONCE(*sp, *sp + 1);
	mb(); /* Ensure update-side operation after counter increment. */
	warn_on_once(rcu_seq_state(*sp) != 1);
}

/* Adjust sequence number for end of update-side operation. */
static inline void rcu_seq_end(unsigned long *sp)
{
	mb(); /* Ensure update-side operation before counter increment. */
	warn_on_once(!rcu_seq_state(*sp));
	WRITE_ONCE(*sp, (*sp | RCU_SEQ_STATE_MASK) + 1);
}

/* Take a snapshot of the update side's sequence number. */
static inline unsigned long rcu_seq_snap(unsigned long *sp)
{
	unsigned long s;

	s = (READ_ONCE(*sp) + 2 * RCU_SEQ_STATE_MASK + 1) & ~RCU_SEQ_STATE_MASK;
	mb(); /* Above access must not bleed into critical section. */
	return s;
}

/* Return the current value the update side's sequence number, no ordering. */
static inline unsigned long rcu_seq_current(unsigned long *sp)
{
	return READ_ONCE(*sp);
}

/*
 * Given a snapshot from rcu_seq_snap(), determine whether or not a
 * full update-side operation has occurred.
 */
static inline bool rcu_seq_done(unsigned long *sp, unsigned long s)
{
	return ULONG_CMP_GE(READ_ONCE(*sp), s);
}

#include <rcu_node_tree.h>

extern int rcu_num_lvls;
extern int num_rcu_lvl[];
extern int rcu_num_nodes;
extern bool rcu_fanout_exact;
extern int rcu_fanout_leaf;
extern int rcu_num_cores;

/*
 * Compute the per-level fanout, either using the exact fanout specified
 * or balancing the tree, depending on the rcu_fanout_exact boot parameter.
 */
static inline void rcu_init_levelspread(int *levelspread, const int *levelcnt)
{
	int i;

	if (rcu_fanout_exact) {
		levelspread[rcu_num_lvls - 1] = rcu_fanout_leaf;
		for (i = rcu_num_lvls - 2; i >= 0; i--)
			levelspread[i] = RCU_FANOUT;
	} else {
		int ccur;
		int cprv;

		cprv = rcu_num_cores;
		for (i = rcu_num_lvls - 1; i >= 0; i--) {
			ccur = levelcnt[i];
			levelspread[i] = (cprv + ccur - 1) / ccur;
			cprv = ccur;
		}
	}
}

/*
 * Do a full breadth-first scan of the rcu_node structures for the
 * specified rcu_state structure.
 */
#define rcu_for_each_node_breadth_first(rsp, rnp) \
	for ((rnp) = &(rsp)->node[0]; \
	     (rnp) < &(rsp)->node[rcu_num_nodes]; (rnp)++)

/*
 * Do a breadth-first scan of the non-leaf rcu_node structures for the
 * specified rcu_state structure.  Note that if there is a singleton
 * rcu_node tree with but one rcu_node structure, this loop is a no-op.
 */
#define rcu_for_each_nonleaf_node_breadth_first(rsp, rnp) \
	for ((rnp) = &(rsp)->node[0]; \
	     (rnp) < (rsp)->level[rcu_num_lvls - 1]; (rnp)++)

/*
 * Scan the leaves of the rcu_node hierarchy for the specified rcu_state
 * structure.  Note that if there is a singleton rcu_node tree with but
 * one rcu_node structure, this loop -will- visit the rcu_node structure.
 * It is still a leaf node, even if it is also the root node.
 */
#define rcu_for_each_leaf_node(rsp, rnp) \
	for ((rnp) = (rsp)->level[rcu_num_lvls - 1]; \
	     (rnp) < &(rsp)->node[rcu_num_nodes]; (rnp)++)

/*
 * Iterate over all possible CPUs in a leaf RCU node.
 */
#define for_each_leaf_node_possible_cpu(rnp, cpu) \
	for ((cpu) = cpumask_next(rnp->grplo - 1, cpu_possible_mask); \
	     cpu <= rnp->grphi; \
	     cpu = cpumask_next((cpu), cpu_possible_mask))

/*
 * Wrappers for the rcu_node::lock acquire and release.
 *
 * Because the rcu_nodes form a tree, the tree traversal locking will observe
 * different lock values, this in turn means that an UNLOCK of one level
 * followed by a LOCK of another level does not imply a full memory barrier;
 * and most importantly transitivity is lost.
 *
 * In order to restore full ordering between tree levels, augment the regular
 * lock acquire functions with smp_mb__after_unlock_lock().
 *
 * As ->lock of struct rcu_node is a __private field, therefore one should use
 * these wrappers rather than directly call raw_spin_{lock,unlock}* on ->lock.
 */
#define raw_spin_lock_rcu_node(p)					\
do {									\
	raw_spin_lock(&ACCESS_PRIVATE(p, lock));			\
	smp_mb__after_unlock_lock();					\
} while (0)

#define raw_spin_unlock_rcu_node(p) raw_spin_unlock(&ACCESS_PRIVATE(p, lock))

#define raw_spin_lock_irq_rcu_node(p)					\
do {									\
	raw_spin_lock_irq(&ACCESS_PRIVATE(p, lock));			\
	smp_mb__after_unlock_lock();					\
} while (0)

#define raw_spin_unlock_irq_rcu_node(p)					\
	raw_spin_unlock_irq(&ACCESS_PRIVATE(p, lock))

#define raw_spin_lock_irqsave_rcu_node(p, flags)			\
do {									\
	raw_spin_lock_irqsave(&ACCESS_PRIVATE(p, lock), flags);	\
	smp_mb__after_unlock_lock();					\
} while (0)

#define raw_spin_unlock_irqrestore_rcu_node(p, flags)			\
	raw_spin_unlock_irqrestore(&ACCESS_PRIVATE(p, lock), flags)	\

#define raw_spin_trylock_rcu_node(p)					\
({									\
	bool ___locked = raw_spin_trylock(&ACCESS_PRIVATE(p, lock));	\
									\
	if (___locked)							\
		smp_mb__after_unlock_lock();				\
	___locked;							\
})

#define RCU_SCHEDULER_INACTIVE	0
#define RCU_SCHEDULER_INIT	1
#define RCU_SCHEDULER_RUNNING	2

enum rcutorture_type {
	RCU_FLAVOR,
	RCU_BH_FLAVOR,
	RCU_SCHED_FLAVOR,
	RCU_TASKS_FLAVOR,
	SRCU_FLAVOR,
	INVALID_RCU_FLAVOR
};

void rcutorture_get_gp_data(enum rcutorture_type test_type, int *flags,
			    unsigned long *gpnum, unsigned long *completed);
void rcutorture_record_test_transition(void);
void rcutorture_record_progress(unsigned long vernum);
void do_trace_rcu_torture_read(const char *rcutorturename,
			       struct rcu_head *rhp,
			       unsigned long secs,
			       unsigned long c_old,
			       unsigned long c);

#endif /* __LINUX_RCU_H */
