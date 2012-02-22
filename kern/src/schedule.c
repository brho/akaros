/* Copyright (c) 2009, 2012 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Scheduling and dispatching. */

#ifdef __SHARC__
#pragma nosharc
#endif

#include <schedule.h>
#include <process.h>
#include <monitor.h>
#include <stdio.h>
#include <assert.h>
#include <atomic.h>
#include <resource.h>
#include <smp.h>
#include <sys/queue.h>

// This could be useful for making scheduling decisions.  
/* Physical coremap: each index is a physical core id, with a proc ptr for
 * whoever *should be or is* running.  Very similar to current, which is what
 * process is *really* running there. */
struct proc *pcoremap[MAX_NUM_CPUS];

/* Tracks which cores are idle, similar to the vcoremap.  Each value is the
 * physical coreid of an unallocated core. */
spinlock_t idle_lock = SPINLOCK_INITIALIZER;
uint32_t idlecoremap[MAX_NUM_CPUS];
uint32_t num_idlecores = 0;
uint32_t num_mgmtcores = 1;

void schedule_init(void)
{
	TAILQ_INIT(&proc_runnablelist);

	/* Ghetto old idle core init */
	/* Init idle cores. Core 0 is the management core. */
	spin_lock(&idle_lock);
#ifdef __CONFIG_DISABLE_SMT__
	/* assumes core0 is the only management core (NIC and monitor functionality
	 * are run there too.  it just adds the odd cores to the idlecoremap */
	assert(!(num_cpus % 2));
	// TODO: consider checking x86 for machines that actually hyperthread
	num_idlecores = num_cpus >> 1;
 #ifdef __CONFIG_ARSC_SERVER__
	// Dedicate one core (core 2) to sysserver, might be able to share wit NIC
	num_mgmtcores++;
	assert(num_cpus >= num_mgmtcores);
	send_kernel_message(2, (amr_t)arsc_server, 0,0,0, KMSG_ROUTINE);
 #endif
	for (int i = 0; i < num_idlecores; i++)
		idlecoremap[i] = (i * 2) + 1;
#else
	// __CONFIG_DISABLE_SMT__
	#ifdef __CONFIG_NETWORKING__
	num_mgmtcores++; // Next core is dedicated to the NIC
	assert(num_cpus >= num_mgmtcores);
	#endif
	#ifdef __CONFIG_APPSERVER__
	#ifdef __CONFIG_DEDICATED_MONITOR__
	num_mgmtcores++; // Next core dedicated to running the kernel monitor
	assert(num_cpus >= num_mgmtcores);
	// Need to subtract 1 from the num_mgmtcores # to get the cores index
	send_kernel_message(num_mgmtcores-1, (amr_t)monitor, 0,0,0, KMSG_ROUTINE);
	#endif
	#endif
 #ifdef __CONFIG_ARSC_SERVER__
	// Dedicate one core (core 2) to sysserver, might be able to share with NIC
	num_mgmtcores++;
	assert(num_cpus >= num_mgmtcores);
	send_kernel_message(num_mgmtcores-1, (amr_t)arsc_server, 0,0,0, KMSG_ROUTINE);
 #endif
	num_idlecores = num_cpus - num_mgmtcores;
	for (int i = 0; i < num_idlecores; i++)
		idlecoremap[i] = i + num_mgmtcores;
#endif /* __CONFIG_DISABLE_SMT__ */
	spin_unlock(&idle_lock);
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

/* Something has changed, and for whatever reason the scheduler should
 * reevaluate things.  Currently, this assumes the calling core is free (since
 * an _S can be proc_run()'d), and it only attempts to run one process at a time
 * (which will suck, longer term, since the manager will just spin this func).
 *
 * Using irqsave spinlocks for now, since this could be called from a timer
 * interrupt handler (though ought to be in a bottom half or something).
 */
void schedule(void)
{
	struct proc *p;
	
	spin_lock_irqsave(&runnablelist_lock);
	/* TODO at the very least, we should do a TAILQ_FOREACH_SAFE, trying to sort
	 * out everyone, but with the highest priority one first (want a priority
	 * queue or something), so people don't have to loop calling schedule().
	 *
	 * Also, we don't want a 'runnable list'.  that's so _S. */
	p = TAILQ_FIRST(&proc_runnablelist);
	if (p) {
		TAILQ_REMOVE(&proc_runnablelist, p, proc_link);
		spin_unlock_irqsave(&runnablelist_lock);
		/* TODO: consider the process's state, or push that into
		 * give_cores/core_request */
		/* lockless reference check, safe since we hold a ref and it's dying.
		 * Note that a process can still be killed right after we do this
		 * check, but give_cores and proc_run can handle that race. */
		if (p->state == PROC_DYING) {
			proc_decref(p);
			return;
		}
		/* TODO: ^^^^^ this is shit i'd like to hide from pluggable kscheds */
		printd("PID of proc i'm running: %d\n", p->pid);
		/* We can safely read is_mcp without locking (i think). */
		if (__proc_is_mcp(p)) {
			/* _Ms need to get some cores, which will call proc_run() internally
			 * (for now) */
			/* TODO: this interface sucks, change it */
			if (!core_request(p))
				schedule_proc(p);	/* can't run, put it back on the queue */
			else
				/* if there's a race on state (like DEATH), it'll get handled by
				 * proc_run or proc_destroy.  TODO: Theoretical race here, since
				 * someone else could make p an _S (in theory), and then we
				 * would be calling this with an inedible ref (which is
				 * currently a concern). */
				proc_run(p); /* trying to run a RUNNABLE_M here */
		} else {
			/* _S proc, just run it */
			proc_run(p);
		}
		/* proc_run will either eat the ref, or we'll decref manually. */
		proc_decref(p);
	} else {
		spin_unlock_irqsave(&runnablelist_lock);
	}
	return;
}

/* A process is asking the ksched to look at its resource desires.  The
 * scheduler is free to ignore this, for its own reasons, so long as it
 * eventually gets around to looking at resource desires. */
void poke_ksched(struct proc *p, int res_type)
{
	/* TODO: probably want something to trigger all res_types */
	/* Consider races with core_req called from other pokes or schedule */
	switch (res_type) {
		case RES_CORES:
			/* TODO: issues with whether or not they are RUNNING.  Need to
			 * change core_request / give_cores. */
			core_request(p);
			break;
		default:
			break;
	}
}

/* Helper function to return a core to the idlemap.  It causes some more lock
 * acquisitions (like in a for loop), but it's a little easier.  Plus, one day
 * we might be able to do this without locks (for the putting). */
void put_idle_core(uint32_t coreid)
{
	spin_lock(&idle_lock);
	idlecoremap[num_idlecores++] = coreid;
	spin_unlock(&idle_lock);
}

/* Normally it'll be the max number of CG cores ever */
uint32_t max_vcores(struct proc *p)
{
#ifdef __CONFIG_DISABLE_SMT__
	return num_cpus >> 1;
#else
	return MAX(1, num_cpus - num_mgmtcores);
#endif /* __CONFIG_DISABLE_SMT__ */
}

/* Ghetto old interface, hacked out of resource.c.  It doesn't even care about
 * the proc yet, but in general the whole core_request bit needs reworked. */
uint32_t proc_wants_cores(struct proc *p, uint32_t *pc_arr, uint32_t amt_new)
{
	uint32_t num_granted;
	/* You should do something smarter than just giving the stuff out.  Like
	 * take in to account priorities, node locations, etc */
	spin_lock(&idle_lock);
	if (num_idlecores >= amt_new) {
		for (int i = 0; i < amt_new; i++) {
			// grab the last one on the list
			pc_arr[i] = idlecoremap[num_idlecores - 1];
			num_idlecores--;
		}
		num_granted = amt_new;
	} else {
		/* In this case, you might want to preempt or do other fun things... */
		num_granted = 0;
	}
	spin_unlock(&idle_lock);
	return num_granted;
}

/************** Debugging **************/
void dump_proclist(struct proc_list *list)
{
	struct proc *p;
	TAILQ_FOREACH(p, list, proc_link)
		printk("PID: %d\n", p->pid);
	return;
}

void print_idlecoremap(void)
{
	spin_lock(&idle_lock);
	printk("There are %d idle cores.\n", num_idlecores);
	for (int i = 0; i < num_idlecores; i++)
		printk("idlecoremap[%d] = %d\n", i, idlecoremap[i]);
	spin_unlock(&idle_lock);
}
