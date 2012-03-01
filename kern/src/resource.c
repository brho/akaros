/*
 * Copyright (c) 2009 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Kernel resource management.
 */

#ifdef __IVY__
#pragma nosharc
#endif

#include <resource.h>
#include <process.h>
#include <smp.h>
#include <stdio.h>
#include <assert.h>
#include <schedule.h>
#include <hashtable.h>

/* This deals with a request for more cores.  The request is already stored in
 * the proc's amt_wanted (it is compared to amt_granted). 
 *
 * It doesn't take the amount requested directly to avoid a race (or holding the
 * proc_lock across the call), and allowing it to be called in other situations,
 * such as if there was not a new request, but it's time to look at the
 * difference between amt_wanted and amt_granted (maybe on a timer interrupt).
 *
 * Will return either the number actually granted or an error code.  This will
 * not decrease the actual amount of cores (e.g. from 5 to 2), but it will
 * transition a process from _M to _S (amt_wanted == 0).
 *
 * This needs a consumable/edible reference of p, in case it doesn't return.
 */
bool core_request(struct proc *p)
{
	uint32_t num_granted, amt_new, amt_wanted, amt_granted;
	uint32_t corelist[MAX_NUM_CPUS]; /* TODO UGH, this could be huge! */

	/* Currently, this is all locked, and there's a variety of races involved,
	 * esp with moving amt_wanted to procdata (TODO).  Will probably want to
	 * copy-in amt_wanted too. */
	spin_lock(&p->proc_lock);
	amt_wanted = p->procdata->res_req[RES_CORES].amt_wanted;
	amt_granted = p->procinfo->res_grant[RES_CORES];

	/* Help them out - if they ask for something impossible, give them 1 so they
	 * can make some progress. (these two are racy). */
	if (amt_wanted > p->procinfo->max_vcores) {
		p->procdata->res_req[RES_CORES].amt_wanted = 1;
	}
	/* TODO: sort how this works with WAITING. */
	if (!amt_wanted) {
		p->procdata->res_req[RES_CORES].amt_wanted = 1;
	}
	/* if they are satisfied, we're done.  There's a slight chance they have
	 * cores, but they aren't running (sched gave them cores while they were
	 * yielding, and now we see them on the run queue). */
	if (amt_wanted <= amt_granted) {
		if (amt_granted) {
			spin_unlock(&p->proc_lock);
			return TRUE;
		} else {
			spin_unlock(&p->proc_lock);
			return FALSE;
		}
	}
	/* otherwise, see what they want.  Current models are simple - it's just a
	 * raw number of cores, and we just give out what we can. */
	amt_new = amt_wanted - amt_granted;
	/* TODO: Could also consider amt_min */

	/* TODO: change this.  this function is really "find me amt_new cores", the
	 * nature of this info depends on how we express desires, and a lot of that
	 * info could be lost through this interface. */
	num_granted = proc_wants_cores(p, corelist, amt_new);

	/* Now, actually give them out */
	if (num_granted) {
		/* give them the cores.  this will start up the extras if RUNNING_M. */
		__proc_give_cores(p, corelist, num_granted);
		/* at some point after giving cores, call proc_run_m() (harmless on
		 * RUNNING_Ms).  You can give small groups of cores, then run them
		 * (which is more efficient than interleaving runs with the gives for
		 * bulk preempted processes). */
		__proc_run_m(p); /* harmless to call this on RUNNING_Ms */
		spin_unlock(&p->proc_lock);
		return TRUE;	/* proc can run (if it isn't already) */
	}
	spin_unlock(&p->proc_lock);
	return FALSE;		/* Not giving them anything more */
}

void print_resources(struct proc *p)
{
	printk("--------------------\n");
	printk("PID: %d\n", p->pid);
	printk("--------------------\n");
	for (int i = 0; i < MAX_NUM_RESOURCES; i++)
		printk("Res type: %02d, amt wanted: %08d, amt granted: %08d\n", i,
		       p->procdata->res_req[i].amt_wanted, p->procinfo->res_grant[i]);
}

void print_all_resources(void)
{
	/* Hash helper */
	void __print_resources(void *item)
	{
		print_resources((struct proc*)item);
	}
	spin_lock(&pid_hash_lock);
	hash_for_each(pid_hash, __print_resources);
	spin_unlock(&pid_hash_lock);
}
