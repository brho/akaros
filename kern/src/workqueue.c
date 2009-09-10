/*
 * Copyright (c) 2009 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 */

#ifdef __SHARC__
#pragma nosharc
#endif

#include <atomic.h>
#include <smp.h>

#include <ros/error.h>

#include <atomic.h>
#include <stdio.h>
#include <workqueue.h>

/*
 * TODO: actually use a queue, which will change some things all over.
 */
void process_workqueue()
{
	struct work TP(TV(t)) work;
	per_cpu_info_t *cpuinfo = &per_cpu_info[core_id()];

	// copy the work in, since we may never return to this stack frame
	spin_lock_irqsave(&cpuinfo->lock);
	work = cpuinfo->workqueue.statics[0];
	spin_unlock_irqsave(&cpuinfo->lock);
	if (work.func) {
		// TODO: possible race with this.  sort it out when we have a queue.
		spin_lock_irqsave(&cpuinfo->lock);
		cpuinfo->workqueue.statics[0].func = 0;
		spin_unlock_irqsave(&cpuinfo->lock);
		// We may never return from this (if it is proc_run)
		work.func(work.data);
	}
}

int enqueue_work(struct workqueue TP(TV(t)) *queue, struct work TP(TV(t)) *job)
{
	error_t retval = 0;
	per_cpu_info_t *cpuinfo = &per_cpu_info[core_id()];

	spin_lock_irqsave(&cpuinfo->lock);
	printd("Enqueuing func 0x%08x and data 0x%08x on core %d.\n",
	       job->func, job->data, core_id());
	if (queue->statics[0].func)
		retval = -ENOMEM;
	else
		queue->statics[0] = *job;
	spin_unlock_irqsave(&cpuinfo->lock);
	return retval;
}
