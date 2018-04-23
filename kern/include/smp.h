/*
 * Copyright (c) 2009 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 */

#pragma once

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
#include <core_set.h>

#define CPU_STATE_IRQ			0
#define CPU_STATE_KERNEL		1
#define CPU_STATE_USER			2
#define CPU_STATE_IDLE			3
#define NR_CPU_STATES			4

static char *cpu_state_names[NR_CPU_STATES] =
{
	"irq",
	"kern",
	"user",
	"idle",
};

struct per_cpu_info {
#ifdef CONFIG_X86
	uintptr_t stacktop;			/* must be first */
	int coreid;					/* must be second */
	int nmi_status;
	uintptr_t nmi_worker_stacktop;
	int vmx_enabled;
	int guest_pcoreid;
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
	int cpu_state;
	uint64_t last_tick_cnt;
	uint64_t state_ticks[NR_CPU_STATES];
	/* TODO: 64b (not sure if we'll need these at all */
#ifdef CONFIG_X86
	taskstate_t *tss;
	segdesc_t *gdt;
#endif
	/* KMSGs */
	spinlock_t immed_amsg_lock;
	struct kernel_msg_list immed_amsgs;
	spinlock_t routine_amsg_lock;
	struct kernel_msg_list routine_amsgs;
	/* profiling -- opaque to all but the profiling code. */
	void *profiling;
}__attribute__((aligned(ARCH_CL_SIZE)));

/* Allows the kernel to figure out what process is running on this core.  Can be
 * used just like a pointer to a struct proc. */
#define current per_cpu_info[core_id()].cur_proc
/* Allows the kernel to figure out what *user* ctx is on this core's stack.  Can
 * be used just like a pointer to a struct user_context.  Note the distinction
 * between kernel and user contexts.  The kernel always returns to its nested,
 * interrupted contexts via iret/etc.  We never do that for user contexts. */
#define current_ctx per_cpu_info[core_id()].cur_ctx

typedef struct per_cpu_info  per_cpu_info_t;
extern per_cpu_info_t per_cpu_info[MAX_NUM_CORES];

#define for_each_core(i) for (int (i) = 0; (i) < num_cores; (i)++)

#define pcpui_ptr(i) &per_cpu_info[(i)]
#define pcpui_var(i, var) per_cpu_info[(i)].var
#define this_pcpui_ptr() pcpui_ptr(core_id())
#define this_pcpui_var(var) pcpui_var(core_id(), var)

/* SMP bootup functions */
void smp_boot(void);
void smp_idle(void) __attribute__((noreturn));
void smp_percpu_init(void); // this must be called by each core individually
void __arch_pcpu_init(uint32_t coreid);	/* each arch has one of these */

void __set_cpu_state(struct per_cpu_info *pcpui, int state);
void reset_cpu_state_ticks(int coreid);

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

void smp_do_in_cores(const struct core_set *cset, void (*func)(void *),
					 void *opaque);

/* Run the handlers for all events in a pcpui ring.  Can run on all cores, or
 * just one core.  'type' selects which event type is handled (0 for all). */
void pcpui_tr_foreach(int coreid, int type);
void pcpui_tr_foreach_all(int type);
void pcpui_tr_reset_all(void);
void pcpui_tr_reset_and_clear_all(void);
