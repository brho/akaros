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
#include <process.h>
#include <workqueue.h>
#include <env.h>

// will want this padded out to cacheline alignment
struct per_cpu_info {
	spinlock_t lock;
	bool preempt_pending;
	struct workqueue workqueue;
#ifdef __i386__
	spinlock_t amsg_lock;
	unsigned amsg_current;
	active_message_t active_msgs[NUM_ACTIVE_MESSAGES];

		 	/* this flag is only ever set when holding the global scheduler lock, and
			 * only cleared (without a lock) by the core in a proc_mgmt IPI.  it is
			 * somewhat arch specific (no in order active messages).  same with the
			 * p_to_run and tf_to_pop */
			// TODO: replace this ghetto with an active message (AM)
			bool proc_ipi_pending;
			/* a proc_startcore IPI will run these.  replace with a dispatch map? */
			struct proc *p_to_run;
			trapframe_t *SAFE tf_to_pop;
#endif
};
extern struct per_cpu_info per_cpu_info[MAX_NUM_CPUS];
extern volatile uint8_t num_cpus;

/* SMP bootup functions */
void smp_boot(void);
void smp_idle(void);

/* SMP utility functions */
int smp_call_function_self(poly_isr_t handler, TV(t) data,
                           handler_wrapper_t** wait_wrapper);
int smp_call_function_all(poly_isr_t handler, TV(t) data,
                          handler_wrapper_t** wait_wrapper);
int smp_call_function_single(uint32_t dest, poly_isr_t handler, TV(t) data,
                             handler_wrapper_t** wait_wrapper);
int smp_call_wait(handler_wrapper_t*SAFE wrapper);

#endif /* !ROS_INC_SMP_H */
