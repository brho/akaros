/* Copyright (c) 2011 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Unbounded concurrent queues, user side.  Check k/i/r/ucq.h or the
 * Documentation for more info. */

#ifndef _UCQ_H
#define _UCQ_H

#include <ros/ucq.h>

void ucq_init_raw(struct ucq *ucq, uintptr_t pg1, uintptr_t pg2);
void ucq_init(struct ucq *ucq);
void ucq_free_pgs(struct ucq *ucq);
int get_ucq_msg(struct ucq *ucq, struct event_msg *msg);

#endif /* _UCQ_H */
