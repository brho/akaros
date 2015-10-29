/* Copyright (c) 2011 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Unbounded concurrent queues, kernel side.  Check k/i/r/ucq.h or the
 * Documentation for more info. */

#pragma once

#include <ros/ucq.h>
#include <process.h>

void send_ucq_msg(struct ucq *ucq, struct proc *p, struct event_msg *msg);
