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
#include <trace.h>
#ifdef CONFIG_X86_64
#include <arch/vm.h>
#endif

#ifdef __SHARC__
typedef sharC_env_t;
#endif

struct per_cpu_info {
#ifdef CONFIG_X86_64
	uintptr_t stacktop;
	/* the rip at the last clock interrupt. For profiling. */
	uintptr_t rip;
	/* virtual machines */
	/* this is all kind of gross, but so it goes. Kmalloc
	 * the vmxarea. It varies in size depending on the architecture.
	 */
	struct vmcs *vmxarea;
	struct vmcs *vmcs;
	pseudodesc_t host_gdt;
	int vmx_enabled;
	void *local_vcpu;
#endif
	spinlock_t lock;
	/* Process management */
	// cur_proc should be valid on all cores that are not management cores.
	struct proc *cur_proc;		/* which process context is loaded */
	struct proc *owning_proc;	/* proc owning the core / cur_ctx */
	uint32_t owning_vcoreid;	/* vcoreid of owning proc (if applicable */
	struct user_context *cur_ctx;	/* user ctx we came in on (can be 0) */
	struct user_context actual_ctx;	/* storage for cur_ctx */
	uint32_t __ctx_depth;		/* don't access directly.  see trap.h. */
	int __lock_checking_enabled;/* == 1, enables spinlock depth checking */
	struct kthread *cur_kthread;/* tracks the running kernel context */
	struct kthread *spare;		/* useful when restarting */
	struct timer_chain tchain;	/* for the per-core alarm */
	unsigned int lock_depth;
	struct trace_ring traces;

#ifdef __SHARC__
	// held spin-locks. this will have to go elsewhere if multiple kernel
	// threads can share a CPU.
	// zra: Used by Ivy. Let me know if this should go elsewhere.
	sharC_env_t sharC_env;
#endif
	/* TODO: 64b (not sure if we'll need these at all */
#ifdef CONFIG_X86
	taskstate_t *tss;
	segdesc_t *gdt;
#endif
	/* KMSGs */
	spinlock_t immed_amsg_lock;
	struct kernel_msg_list NTPTV(a0t) NTPTV(a1t) NTPTV(a2t) immed_amsgs;
	spinlock_t routine_amsg_lock;
	struct kernel_msg_list NTPTV(a0t) NTPTV(a1t) NTPTV(a2t) routine_amsgs;
}__attribute__((aligned(ARCH_CL_SIZE)));

/* Allows the kernel to figure out what process is running on this core.  Can be
 * used just like a pointer to a struct proc. */
#define current per_cpu_info[core_id()].cur_proc
/* Allows the kernel to figure out what *user* ctx is on this core's stack.  Can
 * be used just like a pointer to a struct user_context.  Note the distinction
 * between kernel and user contexts.  The kernel always returns to its nested,
 * interrupted contexts via iret/etc.  We never do that for user contexts. */
#define current_ctx per_cpu_info[core_id()].cur_ctx

typedef struct per_cpu_info NTPTV(t) NTPTV(a0t) NTPTV(a1t) NTPTV(a2t) per_cpu_info_t;

extern per_cpu_info_t (RO per_cpu_info)[MAX_NUM_CPUS];
extern volatile uint32_t RO num_cpus;

/* SMP bootup functions */
void smp_boot(void);
void smp_idle(void) __attribute__((noreturn));
void smp_percpu_init(void); // this must be called by each core individually
void __arch_pcpu_init(uint32_t coreid);	/* each arch has one of these */

/* SMP utility functions */
int smp_call_function_self(isr_t handler, void *data,
                           handler_wrapper_t **wait_wrapper);
int smp_call_function_all(isr_t handler, void *data,
                          handler_wrapper_t **wait_wrapper);
int smp_call_function_single(uint32_t dest, isr_t handler, void *data,
                             handler_wrapper_t **wait_wrapper);
int smp_call_wait(handler_wrapper_t *wrapper);

/* PCPUI Trace Rings: */
struct pcpu_trace_event {
	int							type;
	int							arg0;
	uint64_t					arg1;
};

/* If you want to add a type, use the next available number, increment NR_TYPES,
 * use your own macro, and provide a handler.  Add your handler to
 * pcpui_tr_handlers in smp.c. */
#define PCPUI_TR_TYPE_NULL		0
#define PCPUI_TR_TYPE_KMSG		1
#define PCPUI_TR_TYPE_LOCKS		2
#define PCPUI_NR_TYPES			3

#ifdef CONFIG_TRACE_KMSGS

# define pcpui_trace_kmsg(pcpui, pc)                                           \
{                                                                              \
	struct pcpu_trace_event *e = get_trace_slot_racy(&pcpui->traces);          \
	if (e) {                                                                   \
		e->type = PCPUI_TR_TYPE_KMSG;                                          \
		e->arg1 = pc;                                                          \
	}                                                                          \
}

#else

# define pcpui_trace_kmsg(pcpui, pc)

#endif /* CONFIG_TRACE_KMSGS */


#ifdef CONFIG_TRACE_LOCKS

# define pcpui_trace_locks(pcpui, lock)                                        \
{                                                                              \
	struct pcpu_trace_event *e = get_trace_slot_overwrite(&pcpui->traces);     \
	if (e) {                                                                   \
		e->type = PCPUI_TR_TYPE_LOCKS;                                         \
		e->arg0 = (int)tsc2usec(read_tsc());                                   \
		e->arg1 = (uintptr_t)lock;                                             \
	}                                                                          \
}

#else

# define pcpui_trace_locks(pcpui, lock)

#endif /* CONFIG_TRACE_LOCKS */

/* Run the handlers for all events in a pcpui ring.  Can run on all cores, or
 * just one core.  'type' selects which event type is handled (0 for all). */
void pcpui_tr_foreach(int coreid, int type);
void pcpui_tr_foreach_all(int type);
void pcpui_tr_reset_all(void);
void pcpui_tr_reset_and_clear_all(void);

#endif /* ROS_INC_SMP_H */
