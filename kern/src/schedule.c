/*
 * Copyright (c) 2009 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Scheduling and dispatching.
 */

#include <schedule.h>
#include <process.h>
#include <monitor.h>
#include <stdio.h>
#include <assert.h>
#include <atomic.h>
#include <sys/queue.h>

void schedule_init(void)
{
	TAILQ_INIT(&proc_runnablelist);
	return;
}

void schedule_proc(struct proc *p)
{
	spin_lock_irqsave(&runnablelist_lock);
	printd("Scheduling PID: %d\n", p->env_id);
	TAILQ_INSERT_TAIL(&proc_runnablelist, p, proc_link);
	spin_unlock_irqsave(&runnablelist_lock);
	return;
}

void deschedule_proc(struct proc *p)
{
	spin_lock_irqsave(&runnablelist_lock);
	printd("Descheduling PID: %d\n", p->env_id);
	TAILQ_REMOVE(&proc_runnablelist, p, proc_link);
	spin_unlock_irqsave(&runnablelist_lock);
	return;
}

/*
 * FIFO - just pop the head from the list and run it.
 * Using irqsave spinlocks for now, since this could be called from a timer
 * interrupt handler (though ought to be in a bottom half or something).
 */
void schedule(void)
{
	struct proc *p;
	
	spin_lock_irqsave(&runnablelist_lock);
	p = TAILQ_FIRST(&proc_runnablelist);
	if (p) {
		TAILQ_REMOVE(&proc_runnablelist, p, proc_link);
		spin_unlock_irqsave(&runnablelist_lock);
		printd("PID of proc i'm running: %d\n", p->env_id);
		proc_run(p);
	} else {
		spin_unlock_irqsave(&runnablelist_lock);
		printk("No processes to schedule, enjoy the Monitor!\n");
		while (1)
			monitor(NULL);
	}
	return;
}

void dump_proclist(struct proc_list *list)
{
	struct proc *p;
	TAILQ_FOREACH(p, list, proc_link)
		printk("PID: %d\n", p->env_id);
	return;
}
