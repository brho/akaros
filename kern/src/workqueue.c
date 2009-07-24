/*
 * Copyright (c) 2009 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 */

#include <arch/x86.h>
#include <arch/apic.h>
#include <arch/smp.h>

#include <ros/error.h>

#include <atomic.h>
#include <stdio.h>
#include <workqueue.h>

/*
 * TODO: actually use a queue, which will change some things all over.
 */
void process_workqueue()
{
	struct work work;
	struct per_cpu_info *cpuinfo = &per_cpu_info[coreid()];
	// copy the work in, since we may never return to this stack frame
	spin_lock_irqsave(&cpuinfo->lock);
	work = cpuinfo->workqueue.statics[0];
	spin_unlock_irqsave(&cpuinfo->lock);
	if (work.func) {
		// TODO: possible race with this.  sort it out when we have a queue.
		spin_lock_irqsave(&cpuinfo->lock);
		cpuinfo->workqueue.statics[0].func = 0;
		spin_unlock_irqsave(&cpuinfo->lock);
		// We may never return from this (if it is env_run)
		work.func(work.data);
	}
}

int enqueue_work(struct workqueue *queue, struct work *job)
{
	error_t retval = 0;
	struct per_cpu_info *cpuinfo = &per_cpu_info[coreid()];

	spin_lock_irqsave(&cpuinfo->lock);
	printd("Enqueuing func 0x%08x and data 0x%08x on core %d.\n",
	       job->func, job->data, coreid());
	if (queue->statics[0].func)
		retval = -ENOMEM;
	else
		queue->statics[0] = *job;
	spin_unlock_irqsave(&cpuinfo->lock);
	return retval;
}
