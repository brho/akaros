/*
 * Copyright (c) 2009 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 */

#ifdef __SHARC__
#pragma nosharc
#endif

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

struct per_cpu_info per_cpu_info[MAX_NUM_CPUS];

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
static void __attribute__((noinline, noreturn)) __smp_idle(void)
{
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	clear_rkmsg(pcpui);
	enable_irq();	/* one-shot change to get any IRQs before we halt later */
	while (1) {
		disable_irq();
		process_routine_kmsg();
		try_run_proc();
		cpu_bored();		/* call out to the ksched */
		/* cpu_halt() atomically turns on interrupts and halts the core.
		 * Important to do this, since we could have a RKM come in via an
		 * interrupt right while PRKM is returning, and we wouldn't catch
		 * it. */
		cpu_halt();
		/* interrupts are back on now (given our current semantics) */
	}
	assert(0);
}

void smp_idle(void)
{
	#ifdef CONFIG_RESET_STACKS
	set_stack_pointer(get_stack_top());
	#endif /* CONFIG_RESET_STACKS */
	__smp_idle();
	assert(0);
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
	per_cpu_info[coreid].spare = 0;
	/* Init relevant lists */
	spinlock_init_irqsave(&per_cpu_info[coreid].immed_amsg_lock);
	STAILQ_INIT(&per_cpu_info[coreid].immed_amsgs);
	spinlock_init_irqsave(&per_cpu_info[coreid].routine_amsg_lock);
	STAILQ_INIT(&per_cpu_info[coreid].routine_amsgs);
	/* Initialize the per-core timer chain */
	init_timer_chain(&per_cpu_info[coreid].tchain, set_pcpu_alarm_interrupt);
#ifdef CONFIG_KTHREAD_POISON
	*kstack_bottom_addr(kthread->stacktop) = 0xdeadbeef;
#endif /* CONFIG_KTHREAD_POISON */
	/* Init generic tracing ring */
	trace_buf = kpage_alloc_addr();
	assert(trace_buf);
	trace_ring_init(&pcpui->traces, trace_buf, PGSIZE,
	                sizeof(struct pcpu_trace_event));
	/* Enable full lock debugging, after all pcpui work is done */
	pcpui->__lock_checking_enabled = 1;
}

/* PCPUI Trace Rings: */

static void pcpui_trace_kmsg_handler(void *event, void *data)
{
	struct pcpu_trace_event *te = (struct pcpu_trace_event*)event;
	char *func_name;
	uintptr_t addr;
	addr = te->arg1;
	func_name = get_fn_name(addr);
	printk("\tKMSG %p: %s\n", addr, func_name);
	kfree(func_name);
}

static void pcpui_trace_locks_handler(void *event, void *data)
{
	struct pcpu_trace_event *te = (struct pcpu_trace_event*)event;
	char *func_name;
	uintptr_t lock_addr = te->arg1;
	if (lock_addr > KERN_LOAD_ADDR)
		func_name = get_fn_name(lock_addr);
	else
		func_name = "Dynamic lock";
	printk("Time %uus, lock %p (%s)\n", te->arg0, lock_addr, func_name);
	printk("\t");
	spinlock_debug((spinlock_t*)lock_addr);
	if (lock_addr > KERN_LOAD_ADDR)
		kfree(func_name);
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
	for (int i = 0; i < num_cpus; i++)
		pcpui_tr_foreach(i, type);
}

void pcpui_tr_reset_all(void)
{
	for (int i = 0; i < num_cpus; i++)
		trace_ring_reset(&per_cpu_info[i].traces);
}

void pcpui_tr_reset_and_clear_all(void)
{
	for (int i = 0; i < num_cpus; i++)
		trace_ring_reset_and_clear(&per_cpu_info[i].traces);
}
