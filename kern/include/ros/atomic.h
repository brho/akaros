/* Copyright (c) 2010 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Locking and atomics that are part of the kernel interface. */

#ifndef ROS_INC_ATOMIC_H
#define ROS_INC_ATOMIC_H

#include <ros/common.h>

/* The seq counters are used by userspace to see if the kernel is updating
 * something or if something is valid, such as the vcore->pcore mapping.  The
 * way a reader can tell nothing has changed is to read the counter before and
 * after.  If the value has changed, the reader needs to re-read.  If the value
 * is odd, a write is in progress or it is otherwise invalid/locked. */

typedef uint8_t seq_ctr_t;
#define SEQCTR_INITIALIZER 0

static inline bool seq_is_locked(seq_ctr_t seq_ctr);
static inline bool seqctr_retry(seq_ctr_t old_ctr, seq_ctr_t new_ctr);

/* Basic helpers for readers.  Ex:
 * do {
 * 		seq_ctr_t seq = kernel_maintained_seq_ctr
 * 		read_data_whatever();
 * } while (seqctr_retry(seq, kernel_maintained_seq_ctr);
 */
static inline bool seq_is_locked(seq_ctr_t seq_ctr)
{
	return seq_ctr % 2;
}

static inline bool seqctr_retry(seq_ctr_t old_ctr, seq_ctr_t new_ctr)
{
	return (seq_is_locked(old_ctr)) || (old_ctr != new_ctr);	
}

#endif /* !ROS_INC_ATOMIC_H */
