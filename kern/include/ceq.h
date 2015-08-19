/* Copyright (c) 2015 Google Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Coalescing Event Queue: encapuslates the essence of epoll/kqueue in shared
 * memory: a dense array of sticky status bits.
 *
 * Kernel side (producer) */

#ifndef ROS_KERN_CEQ_H
#define ROS_KERN_CEQ_H

#include <ros/ceq.h>
#include <process.h>

void send_ceq_msg(struct ceq *ceq, struct proc *p, struct event_msg *msg);

#endif /* ROS_KERN_CEQ_H */
