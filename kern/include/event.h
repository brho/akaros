/* Copyright (c) 2011 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Kernel utility functions for sending events and notifications (IPIs) to
 * processes. */

#ifndef ROS_KERN_EVENT_H
#define ROS_KERN_EVENT_H

#include <ros/event.h>
#include <process.h>

void send_event(struct proc *p, struct event_queue *ev_q, struct event_msg *msg,
                uint32_t vcoreid);
void send_kernel_event(struct proc *p, struct event_msg *msg, uint32_t vcoreid);
void post_vcore_event(struct proc *p, struct event_msg *msg, uint32_t vcoreid);

#endif /* ROS_KERN_EVENT_H */
