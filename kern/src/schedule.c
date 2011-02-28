/*
 * Copyright (c) 2009 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Scheduling and dispatching.
 */

#ifdef __SHARC__
#pragma nosharc
#endif

#include <schedule.h>
#include <process.h>
#include <monitor.h>
#include <stdio.h>
#include <assert.h>
#include <atomic.h>
#include <sys/queue.h>

// This could be useful for making scheduling decisions.  
/* Physical coremap: each index is a physical core id, with a proc ptr for
 * whoever *should be or is* running.  Very similar to current, which is what
 * process is *really* running there. */
struct proc *pcoremap[MAX_NUM_CPUS];

void schedule_init(void)
{
	TAILQ_INIT(&proc_runnablelist);
	return;
}

void schedule_proc(struct proc *p)
{
	/* up the refcnt since we are storing the reference */
	proc_incref(p, 1);
	spin_lock_irqsave(&runnablelist_lock);
	printd("Scheduling PID: %d\n", p->pid);
	TAILQ_INSERT_TAIL(&proc_runnablelist, p, proc_link);
	spin_unlock_irqsave(&runnablelist_lock);
	return;
}

/* TODO: race here.  it's possible that p was already removed from the
 * list (by schedule()), while proc_destroy is trying to remove it from the
 * list.  schedule()'s proc_run() won't actually run it (since it's DYING), but
 * this code will probably fuck up.  Having TAILQ_REMOVE not hurt will help. */
void deschedule_proc(struct proc *p)
{
	spin_lock_irqsave(&runnablelist_lock);
	printd("Descheduling PID: %d\n", p->pid);
	TAILQ_REMOVE(&proc_runnablelist, p, proc_link);
	spin_unlock_irqsave(&runnablelist_lock);
	/* down the refcnt, since its no longer stored */
	proc_decref(p);
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
		printd("PID of proc i'm running: %d\n", p->pid);
		/* proc_run will either eat the ref, or we'll decref manually. */
		proc_run(p);
		proc_decref(p);
	} else {
		spin_unlock_irqsave(&runnablelist_lock);
	}
	return;
}

void dump_proclist(struct proc_list *list)
{
	struct proc *p;
	TAILQ_FOREACH(p, list, proc_link)
		printk("PID: %d\n", p->pid);
	return;
}
