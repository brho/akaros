/*
 * Copyright (c) 2009 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 */

#ifndef ROS_INC_SMP_H
#define ROS_INC_SMP_H

/* SMP related functions */

#include <arch/smp.h>
#include <arch/types.h>
#include <trap.h>
#include <atomic.h>
#include <workqueue.h>
#include <env.h>

// will want this padded out to cacheline alignment
struct per_cpu_info {
	uint32_t lock;
	bool preempt_pending;
	struct workqueue workqueue;
};
extern struct per_cpu_info  per_cpu_info[MAX_NUM_CPUS];
extern volatile uint8_t num_cpus;

/* SMP bootup functions */
void smp_boot(void);
void smp_idle(void);

/* SMP utility functions */
int smp_call_function_self(poly_isr_t handler, TV(t) data,
                           handler_wrapper_t** wait_wrapper);
int smp_call_function_all(poly_isr_t handler, TV(t) data,
                          handler_wrapper_t** wait_wrapper);
int smp_call_function_single(uint8_t dest, poly_isr_t handler, TV(t) data,
                             handler_wrapper_t** wait_wrapper);
int smp_call_wait(handler_wrapper_t*SAFE wrapper);

#endif /* !ROS_INC_SMP_H */
