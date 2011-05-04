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
#include <syscall.h>
#include <alarm.h>

#ifdef __SHARC__
typedef sharC_env_t;
#endif

struct per_cpu_info {
	spinlock_t lock;
	/* Process management */
	// cur_proc should be valid on all cores that are not management cores.
	struct proc *cur_proc;
	struct trapframe *cur_tf;	/* user tf we came in on (can be 0) */
	struct trapframe actual_tf;	/* storage for cur_tf */
	struct syscall *cur_sysc;	/* ptr is into cur_proc's address space */
	struct kthread *spare;		/* useful when restarting */
	struct timer_chain tchain;	/* for the per-core alarm */

#ifdef __SHARC__
	// held spin-locks. this will have to go elsewhere if multiple kernel
	// threads can share a CPU.
	// zra: Used by Ivy. Let me know if this should go elsewhere.
	sharC_env_t sharC_env;
#endif
#ifdef __i386__
	taskstate_t *tss;
	segdesc_t *gdt;
#endif
	/* KMSGs */
	spinlock_t immed_amsg_lock;
	struct kernel_msg_list NTPTV(a0t) NTPTV(a1t) NTPTV(a2t) immed_amsgs;
	spinlock_t routine_amsg_lock;
	struct kernel_msg_list NTPTV(a0t) NTPTV(a1t) NTPTV(a2t) routine_amsgs;
}__attribute__((aligned(HW_CACHE_ALIGN)));

/* Allows the kernel to figure out what process is running on this core.  Can be
 * used just like a pointer to a struct proc. */
#define current per_cpu_info[core_id()].cur_proc
/* Allows the kernel to figure out what *user* tf is on this core's stack.  Can
 * be used just like a pointer to a struct Trapframe.  Note the distinction
 * between kernel and user contexts.  The kernel always returns to its nested,
 * interrupted contexts via iret/etc.  We never do that for user contexts. */
#define current_tf per_cpu_info[core_id()].cur_tf

typedef struct per_cpu_info NTPTV(t) NTPTV(a0t) NTPTV(a1t) NTPTV(a2t) per_cpu_info_t;

extern per_cpu_info_t (RO per_cpu_info)[MAX_NUM_CPUS];
extern volatile uint32_t RO num_cpus;

/* SMP bootup functions */
void smp_boot(void);
void smp_idle(void) __attribute__((noreturn));
void smp_percpu_init(void); // this must be called by each core individually
void __arch_pcpu_init(uint32_t coreid);	/* each arch has one of these */

/* SMP utility functions */
int smp_call_function_self(poly_isr_t handler, TV(t) data,
                           handler_wrapper_t** wait_wrapper);
int smp_call_function_all(poly_isr_t handler, TV(t) data,
                          handler_wrapper_t** wait_wrapper);
int smp_call_function_single(uint32_t dest, poly_isr_t handler, TV(t) data,
                             handler_wrapper_t** wait_wrapper);
int smp_call_wait(handler_wrapper_t*SAFE wrapper);

#endif /* !ROS_INC_SMP_H */
