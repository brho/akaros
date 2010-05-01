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
#include <manager.h>
#include <trap.h>

struct per_cpu_info per_cpu_info[MAX_NUM_CPUS];

// tracks number of global waits on smp_calls, must be <= NUM_HANDLER_WRAPPERS
atomic_t outstanding_calls = 0;

/* All cores end up calling this whenever there is nothing left to do.  Non-zero
 * cores call it when they are done booting.  Other cases include after getting
 * a DEATH IPI.
 * - Management cores (core 0 for now) call manager, which should never return.
 * - Worker cores halt and wake up when interrupted, do any work on their work
 *   queue, then halt again.
 *
 * TODO: think about resetting the stack pointer at the beginning for worker
 * cores. (keeps the stack from growing if we never go back to userspace).
 * TODO: think about unifying the manager into a workqueue function, so we don't
 * need to check mgmt_core in here.  it gets a little ugly, since there are
 * other places where we check for mgmt and might not smp_idle / call manager.
 */
void smp_idle(void)
{
	int8_t state = 0;
	per_cpu_info_t *myinfo = &per_cpu_info[core_id()];

	if (!management_core()) {
		enable_irq();
		while (1) {
			process_routine_kmsg();
			cpu_halt();
		}
	} else {
		/* techincally, this check is arch dependent.  i want to know if it
		 * happens.  the enabling/disabling could be interesting. */
		enable_irqsave(&state);
		if (!STAILQ_EMPTY(&myinfo->immed_amsgs) ||
		        !STAILQ_EMPTY(&myinfo->routine_amsgs)) 
			printk("[kernel] kmsgs in smp_idle() on a management core.\n");
		process_routine_kmsg();
		disable_irqsave(&state);
		manager();
	}
	assert(0);
}

#ifdef __CONFIG_EXPER_TRADPROC__
/* For experiments with per-core schedulers (traditional).  This checks the
 * runqueue, and if there is something there, it runs in.  Note this does
 * nothing for whoever was running here.  Consider saving and restoring them,
 * resetting current, etc. */
void local_schedule(void)
{
	struct per_cpu_info *my_info = &per_cpu_info[core_id()];
	struct proc *next_to_run;

	spin_lock_irqsave(&my_info->runqueue_lock);
	next_to_run = TAILQ_FIRST(&my_info->runqueue);
	if (next_to_run)
		TAILQ_REMOVE(&my_info->runqueue, next_to_run, proc_link);
	spin_unlock_irqsave(&my_info->runqueue_lock);
	if (!next_to_run)
		return;
	assert(next_to_run->state == PROC_RUNNING_M); // FILTHY HACK
	printd("Core %d trying to run proc %08p\n", core_id(), next_to_run);
	void proc_run_hand(struct trapframe *tf, uint32_t src_id, void *p, void *a1,
	                   void *a2)
	{
		proc_run((struct proc*)p);
	}
	send_kernel_message(core_id(), proc_run_hand, (void*)next_to_run, 0, 0,
	                    KMSG_ROUTINE);
	return;
}

void local_schedule_proc(uint32_t core, struct proc *p)
{
	assert(core); // for sanity don't put them on core0 or any management core
	struct per_cpu_info *my_info = &per_cpu_info[core];
	spin_lock_irqsave(&my_info->runqueue_lock);
	TAILQ_INSERT_TAIL(&my_info->runqueue, p, proc_link);
	printd("SCHED: inserting proc %p on core %d\n", p, core);
	spin_unlock_irqsave(&my_info->runqueue_lock);
}

/* ghetto func to act like a load balancer.  for now, it just looks at the head
 * of every other cpu's queue. */
void load_balance(void)
{
	struct per_cpu_info *other_info;
	struct proc *dummy;

	for (int i = 0; i < num_cpus; i++) {
		other_info = &per_cpu_info[i];
		spin_lock_irqsave(&other_info->runqueue_lock);
		dummy = TAILQ_FIRST(&other_info->runqueue);
		spin_unlock_irqsave(&other_info->runqueue_lock);
	}
}

#endif /* __CONFIG_EXPER_TRADPROC__ */
