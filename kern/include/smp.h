/*
 * Copyright (c) 2009 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 */

#ifndef ROS_INC_SMP_H
#define ROS_INC_SMP_H

/* SMP related functions */

#include <arch/smp.h>
#include <ros/common.h>
#include <sys/queue.h>
#include <trap.h>
#include <atomic.h>
#include <process.h>
#include <workqueue.h>

#ifdef __SHARC__
typedef sharC_env_t;
#endif

struct per_cpu_info {
	spinlock_t lock;
	struct proc *cur_proc;
	trapframe_t *cur_tf;
	bool preempt_pending;
	struct workqueue NTPTV(t) workqueue;

#ifdef __SHARC__
	// held spin-locks. this will have to go elsewhere if multiple kernel
	// threads can share a CPU.
	// zra: Used by Ivy. Let me know if this should go elsewhere.
	sharC_env_t sharC_env;
#endif
#ifdef __i386__
	segdesc_t *gdt;
#endif

	spinlock_t amsg_lock;
	struct active_msg_list NTPTV(a0t) NTPTV(a1t) NTPTV(a2t) active_msgs;
}__attribute__((aligned(HW_CACHE_ALIGN)));

typedef struct per_cpu_info NTPTV(t) NTPTV(a0t) NTPTV(a1t) NTPTV(a2t) per_cpu_info_t;

extern per_cpu_info_t (RO per_cpu_info)[MAX_NUM_CPUS];
extern volatile uint32_t RO num_cpus;

/* SMP bootup functions */
void smp_boot(void);
void smp_idle(void);
void smp_percpu_init(void); // this must be called by each core individually

/* SMP utility functions */
int smp_call_function_self(poly_isr_t handler, TV(t) data,
                           handler_wrapper_t** wait_wrapper);
int smp_call_function_all(poly_isr_t handler, TV(t) data,
                          handler_wrapper_t** wait_wrapper);
int smp_call_function_single(uint32_t dest, poly_isr_t handler, TV(t) data,
                             handler_wrapper_t** wait_wrapper);
int smp_call_wait(handler_wrapper_t*SAFE wrapper);

#endif /* !ROS_INC_SMP_H */
