/*
 * Copyright (c) 2009 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 */

#ifndef ROS_ARCH_SMP_H
#define ROS_ARCH_SMP_H

#include <atomic.h>

// be careful changing this, esp if you go over 16
#define NUM_HANDLER_WRAPPERS		5

struct HandlerWrapper {
	checklist_t* cpu_list;
	uint8_t vector;
};

typedef struct HandlerWrapper handler_wrapper_t;

#endif /* !ROS_ARCH_SMP_H */
