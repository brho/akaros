/*
 * Copyright (c) 2009 The Regents of the University of California
 * See LICENSE for details.
 */

#include <arch/x86.h>
#include <arch/apic.h>
#include <arch/smp.h>
#include <arch/atomic.h>

#include <workqueue.h>

/*
 * TODO: actually use a queue, which will change some things all over.
 */
void process_workqueue()
{
	work_t work;
	per_cpu_info_t *cpuinfo = &per_cpu_info[lapic_get_id()];
	// copy the work in, since we may never return to this stack frame
	spin_lock_irqsave(&cpuinfo->lock);
	work = cpuinfo->delayed_work;
	spin_unlock_irqsave(&cpuinfo->lock);
	if (work.func) {
		// TODO: possible race with this.  sort it out when we have a queue.
		spin_lock_irqsave(&cpuinfo->lock);
		cpuinfo->delayed_work.func = 0;
		spin_unlock_irqsave(&cpuinfo->lock);
		// We may never return from this (if it is env_run)
		work.func(work.data);
	}
}
