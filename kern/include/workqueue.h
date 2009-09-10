/*
 * Copyright (c) 2009 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Workqueue: This is a todo list of func, void* that get executed whenever
 * process_workqueue is called.  Typically, this is called from smp_idle().
 * Note that every core will run this, so be careful with dynamic memory mgmt.
 */

#ifndef ROS_KERN_WORKQUEUE_H
#define ROS_KERN_WORKQUEUE_H

#include <sys/queue.h>
#include <ros/common.h>
#include <env.h>

typedef void (*func_t)(TV(t) data);
struct work {
	LIST_ENTRY(work) work_link;
	func_t func;
	TV(t) data;
};

// TODO make these dynamic and hold more than 1.  might want better list macros.
#define WORKQUEUE_ELEMENTS 1
struct workqueue {
	struct work TP(TV(t)) statics[WORKQUEUE_ELEMENTS];
};

void process_workqueue(void);
// For now, the caller should free their struct work after this call
int enqueue_work(struct workqueue TP(TV(t)) *queue, struct work TP(TV(t)) *job);

#endif /* ROS_KERN_WORKQUEUE_H */
