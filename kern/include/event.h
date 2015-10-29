/* Copyright (c) 2011 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Kernel utility functions for sending events and notifications (IPIs) to
 * processes. */

#pragma once

#include <ros/event.h>
#include <ros/bits/posix_signum.h>
#include <process.h>

void send_event(struct proc *p, struct event_queue *ev_q, struct event_msg *msg,
                uint32_t vcoreid);
void send_kernel_event(struct proc *p, struct event_msg *msg, uint32_t vcoreid);
void post_vcore_event(struct proc *p, struct event_msg *msg, uint32_t vcoreid,
                      int ev_flags);
void send_posix_signal(struct proc *p, int sig_nr);
