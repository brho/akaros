/*
 * Copyright (c) 2009 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Kernel resource management.
 */

#ifndef ROS_KERN_RESOURCE_H
#define ROS_KERN_RESOURCE_H

#include <ros/resource.h>
#include <error.h>
#include <ros/common.h>
#include <arch/trap.h>
#include <process.h>

bool core_request(struct proc *p);

void print_resources(struct proc *p);
void print_all_resources(void);

#endif // !ROS_KERN_RESOURCE_H
