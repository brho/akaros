/*
 * Copyright (c) 2009 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 */

#include <arch/arch.h>
#include <atomic.h>
#include <smp.h>
#include <error.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <pmap.h>
#include <process.h>
#include <schedule.h>
#include <trap.h>
#include <trace.h>
#include <kdebug.h>
#include <kmalloc.h>
#include <core_set.h>
#include <completion.h>
#include <rcu.h>

struct all_cpu_work {
	struct completion comp;
	void (*func)(void *);
	void *opaque;
};

struct per_cpu_info per_cpu_info[MAX_NUM_CORES];

// tracks number of global waits on smp_calls, must be <= NUM_HANDLER_WRAPPERS
atomic_t outstanding_calls = 0;

/* Helper for running a proc (if we should).  Lots of repetition with
 * proc_restartcore */
static void try_run_proc(void)
{
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	/* There was a process running here, and we should return to it. */
	if (pcpui->owning_proc) {
		assert(!pcpui->cur_kthread->sysc);
		assert(pcpui->cur_ctx);
		__proc_startcore(pcpui->owning_proc, pcpui->cur_ctx);
		assert(0);
	} else {
		/* Make sure we have abandoned core.  It's possible to have an owner
		 * without a current (smp_idle, __startcore, __death). */
		abandon_core();
	}
}

/* All cores end up calling this whenever there is nothing left to do or they
 * don't know explicitly what to do.  Non-zero cores call it when they are done
 * booting.  Other cases include after getting a DEATH IPI.
 *
 * All cores attempt to run the context of any owning proc.  Barring that, they
 * halt and wake up when interrupted, do any work on their work queue, then halt
 * again.  In between, the ksched gets a chance to tell it to do something else,
 * or perhaps to halt in another manner. */
static void __attribute__((noreturn)) __smp_idle(void *arg)
{
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];

	pcpui->cur_kthread->flags = KTH_DEFAULT_FLAGS;
	while (1) {
		/* This might wake a kthread (the gp ktask), so be sure to run PRKM
		 * after reporting the quiescent state. */
		rcu_report_qs();
		/* If this runs an RKM, we'll call smp_idle from the top. */
		process_routine_kmsg();
		try_run_proc();
		cpu_bored();		/* call out to the ksched */
		/* cpu_halt() atomically turns on interrupts and halts the core.
		 * Important to do this, since we could have a RKM come in via an
		 * interrupt right while PRKM is returning, and we wouldn't catch
		 * it.  When it returns, IRQs are back off. */
		__set_cpu_state(pcpui, CPU_STATE_IDLE);
		cpu_halt();
		__set_cpu_state(pcpui, CPU_STATE_KERNEL);
	}
	assert(0);
}

void smp_idle(void)
{
	disable_irq();
	__reset_stack_pointer(0, get_stack_top(), __smp_idle);
}

/* Arch-independent per-cpu initialization.  This will call the arch dependent
 * init first. */
void smp_percpu_init(void)
{
	uint32_t coreid = core_id();
	struct per_cpu_info *pcpui = &per_cpu_info[coreid];
	void *trace_buf;
	struct kthread *kthread;
	/* Don't initialize __ctx_depth here, since it is already 1 (at least on
	 * x86), since this runs in irq context. */
	/* Do this first */
	__arch_pcpu_init(coreid);
	/* init our kthread (tracks our currently running context) */
	kthread = __kthread_zalloc();
	kthread->stacktop = get_stack_top();	/* assumes we're on the 1st page */
	pcpui->cur_kthread = kthread;
	/* Treat the startup threads as ktasks.  This will last until smp_idle when
	 * they clear it, either in anticipation of being a user-backing kthread or
	 * to handle an RKM. */
	kthread->flags = KTH_KTASK_FLAGS;
	per_cpu_info[coreid].spare = 0;
	/* Init relevant lists */
	spinlock_init_irqsave(&per_cpu_info[coreid].immed_amsg_lock);
	STAILQ_INIT(&per_cpu_info[coreid].immed_amsgs);
	spinlock_init_irqsave(&per_cpu_info[coreid].routine_amsg_lock);
	STAILQ_INIT(&per_cpu_info[coreid].routine_amsgs);
	/* Initialize the per-core timer chain */
	init_timer_chain(&per_cpu_info[coreid].tchain, set_pcpu_alarm_interrupt);
	/* Init generic tracing ring */
	trace_buf = kpage_alloc_addr();
	assert(trace_buf);
	trace_ring_init(&pcpui->traces, trace_buf, PGSIZE,
	                sizeof(struct pcpu_trace_event));
	for (int i = 0; i < NR_CPU_STATES; i++)
		pcpui->state_ticks[i] = 0;
	pcpui->last_tick_cnt = read_tsc();
	/* Core 0 is in the KERNEL state, called from smp_boot.  The other cores are
	 * too, at least on x86, where we were called from asm (woken by POKE). */
	pcpui->cpu_state = CPU_STATE_KERNEL;
	/* Enable full lock debugging, after all pcpui work is done */
	pcpui->__lock_checking_enabled = 1;
}

/* it's actually okay to set the state to the existing state.  originally, it
 * was a bug in the state tracking, but it is possible, at least on x86, to have
 * a halted core (state IDLE) get woken up by an IRQ that does not trigger the
 * IRQ handling state.  for example, there is the I_POKE_CORE ipi.  smp_idle
 * will just sleep again, and reset the state from IDLE to IDLE. */
void __set_cpu_state(struct per_cpu_info *pcpui, int state)
{
	uint64_t now_ticks;
	assert(!irq_is_enabled());
	/* TODO: could put in an option to enable/disable state tracking. */
	now_ticks = read_tsc();
	pcpui->state_ticks[pcpui->cpu_state] += now_ticks - pcpui->last_tick_cnt;
	/* TODO: if the state was user, we could account for the vcore's time,
	 * similar to the total_ticks in struct vcore.  the difference is that the
	 * total_ticks tracks the vcore's virtual time, while this tracks user time.
	 * something like vcore->user_ticks. */
	pcpui->cpu_state = state;
	pcpui->last_tick_cnt = now_ticks;
}

void reset_cpu_state_ticks(int coreid)
{
	struct per_cpu_info *pcpui = &per_cpu_info[coreid];
	uint64_t now_ticks;
	if (coreid >= num_cores)
		return;
	/* need to update last_tick_cnt, so the current value doesn't get added in
	 * next time we update */
	now_ticks = read_tsc();
	for (int i = 0; i < NR_CPU_STATES; i++) {
		pcpui->state_ticks[i] = 0;
		pcpui->last_tick_cnt = now_ticks;
	}
}

/* PCPUI Trace Rings: */

static void pcpui_trace_kmsg_handler(void *event, void *data)
{
	struct pcpu_trace_event *te = (struct pcpu_trace_event*)event;
	uintptr_t addr;

	addr = te->arg1;
	printk("\tKMSG %p: %s\n", addr, get_fn_name(addr));
}

static void pcpui_trace_locks_handler(void *event, void *data)
{
	struct pcpu_trace_event *te = (struct pcpu_trace_event*)event;
	const char *func_name;
	uintptr_t lock_addr = te->arg1;

	if (lock_addr > KERN_LOAD_ADDR)
		func_name = get_fn_name(lock_addr);
	else
		func_name = "Dynamic lock";
	print_lock();
	printk("Time %uus, lock %p (%s)\n", te->arg0, lock_addr, func_name);
	printk("\t");
	spinlock_debug((spinlock_t*)lock_addr);
	print_unlock();
}

/* Add specific trace handlers here: */
trace_handler_t pcpui_tr_handlers[PCPUI_NR_TYPES] = {
                                  0,
                                  pcpui_trace_kmsg_handler,
                                  pcpui_trace_locks_handler,
                                  };

/* Generic handler for the pcpui ring.  Will switch out to the appropriate
 * type's handler */
static void pcpui_trace_fn(void *event, void *data)
{
	struct pcpu_trace_event *te = (struct pcpu_trace_event*)event;
	int desired_type = (int)(long)data;
	if (te->type >= PCPUI_NR_TYPES)
		printk("Bad trace type %d\n", te->type);
	/* desired_type == 0 means all types */
	if (desired_type && desired_type != te->type)
		return;
	if (pcpui_tr_handlers[te->type])
		pcpui_tr_handlers[te->type](event, data);
}

void pcpui_tr_foreach(int coreid, int type)
{
	struct trace_ring *tr = &per_cpu_info[coreid].traces;
	assert(tr);
	printk("\n\nTrace Ring on Core %d\n--------------\n", coreid);
	trace_ring_foreach(tr, pcpui_trace_fn, (void*)(long)type);
}

void pcpui_tr_foreach_all(int type)
{
	for (int i = 0; i < num_cores; i++)
		pcpui_tr_foreach(i, type);
}

void pcpui_tr_reset_all(void)
{
	for (int i = 0; i < num_cores; i++)
		trace_ring_reset(&per_cpu_info[i].traces);
}

void pcpui_tr_reset_and_clear_all(void)
{
	for (int i = 0; i < num_cores; i++)
		trace_ring_reset_and_clear(&per_cpu_info[i].traces);
}

static void smp_do_core_work(uint32_t srcid, long a0, long a1, long a2)
{
	struct all_cpu_work *acw = (struct all_cpu_work *) a0;

	acw->func(acw->opaque);
	completion_complete(&acw->comp, 1);
}

void smp_do_in_cores(const struct core_set *cset, void (*func)(void *),
					 void *opaque)
{
	int cpu = core_id();
	struct all_cpu_work acw;

	memset(&acw, 0, sizeof(acw));
	completion_init(&acw.comp, core_set_remote_count(cset));
	acw.func = func;
	acw.opaque = opaque;

	for (int i = 0; i < num_cores; i++) {
		if (core_set_getcpu(cset, i)) {
			if (i == cpu)
				func(opaque);
			else
				send_kernel_message(i, smp_do_core_work, (long) &acw, 0, 0,
									KMSG_ROUTINE);
		}
	}
	completion_wait(&acw.comp);
}
