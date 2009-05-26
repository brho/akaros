/*
 * Copyright (c) 2009 The Regents of the University of California
 * See LICENSE for details.
 */

#include <arch/x86.h>
#include <arch/apic.h>
#include <arch/smp.h>
#include <workqueue.h>

/*
 * TODO: actually use a queue, which will change some things all over.
 */
void process_workqueue()
{
	work_t work;
	// copy the work in, since we may never return to this stack frame
	work = per_cpu_info[lapic_get_id()].delayed_work;
	if (work.func) {
		// TODO: possible race with this.  sort it out when we have a queue.
		// probably want a spin_lock_irqsave
		per_cpu_info[lapic_get_id()].delayed_work.func = 0;
		// We may never return from this (if it is env_run)
		work.func(work.data);
	}
}
