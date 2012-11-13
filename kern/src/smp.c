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

struct per_cpu_info per_cpu_info[MAX_NUM_CPUS];

// tracks number of global waits on smp_calls, must be <= NUM_HANDLER_WRAPPERS
atomic_t outstanding_calls = 0;

/* Helper for running a proc (if we should).  Lots of repetition with
 * proc_restartcore */
static void try_run_proc(void)
{
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];

	disable_irq();
	/* There was a process running here, and we should return to it. */
	if (pcpui->owning_proc) {
		proc_restartcore();
		assert(0);
	}
}

/* All cores end up calling this whenever there is nothing left to do or they
 * don't know explicitly what to do.  Non-zero cores call it when they are done
 * booting.  Other cases include after getting a DEATH IPI.
 *
 * All cores attempt to run the context of any owning proc.  Barring that, the
 * cores enter a loop.  They halt and wake up when interrupted, do any work on
 * their work queue, then halt again.  In between, the ksched gets a chance to
 * tell it to do something else, or perhaps to halt in another manner. */
static void __attribute__((noinline, noreturn)) __smp_idle(void)
{
	/* TODO: idle, abandon_core(), and proc_restartcore() need cleaned up */
	enable_irq();	/* get any IRQs before we halt later */
	try_run_proc();
	/* if we made it here, we truly want to idle */
	/* in the future, we may need to proactively leave process context here.
	 * for now, it is possible to have a current loaded, even if we are idle
	 * (and presumably about to execute a kmsg or fire up a vcore). */
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
	#ifdef __CONFIG_RESET_STACKS__
	set_stack_pointer(get_stack_top());
	#endif /* __CONFIG_RESET_STACKS__ */
	__smp_idle();
	assert(0);
}

/* Arch-independent per-cpu initialization.  This will call the arch dependent
 * init first. */
void smp_percpu_init(void)
{
	uint32_t coreid = core_id();
	/* Do this first */
	__arch_pcpu_init(coreid);
	per_cpu_info[coreid].spare = 0;
	/* Init relevant lists */
	spinlock_init(&per_cpu_info[coreid].immed_amsg_lock);
	STAILQ_INIT(&per_cpu_info[coreid].immed_amsgs);
	spinlock_init(&per_cpu_info[coreid].routine_amsg_lock);
	STAILQ_INIT(&per_cpu_info[coreid].routine_amsgs);
	/* Initialize the per-core timer chain */
	init_timer_chain(&per_cpu_info[coreid].tchain, set_pcpu_alarm_interrupt);
#ifdef __CONFIG_KTHREAD_POISON__
	/* TODO: KTHR-STACK */
	uintptr_t *poison = (uintptr_t*)ROUNDDOWN(get_stack_top() - 1, PGSIZE);
	*poison = 0xdeadbeef;
#endif /* __CONFIG_KTHREAD_POISON__ */
}
