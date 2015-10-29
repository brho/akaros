/* Copyright (c) 2011 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Unbounded concurrent queues, user side.  Check k/i/r/ucq.h or the
 * Documentation for more info. */

#pragma once

#include <ros/ucq.h>

__BEGIN_DECLS

void ucq_init_raw(struct ucq *ucq, uintptr_t pg1, uintptr_t pg2);
void ucq_init(struct ucq *ucq);
void ucq_free_pgs(struct ucq *ucq);
bool get_ucq_msg(struct ucq *ucq, struct event_msg *msg);
bool ucq_is_empty(struct ucq *ucq);

__END_DECLS
