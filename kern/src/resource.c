/*
 * Copyright (c) 2009 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Kernel resource management.
 */

#include <resource.h>
#include <process.h>
#include <stdio.h>
#include <assert.h>

/* This deals with a request for more cores.  The request is already stored in
 * the proc's amt_wanted (it is compared to amt_granted). 
 *
 * It doesn't take the amount requested directly to avoid a race (or holding the
 * proc_lock across the call), and allowing it to be called in other situations,
 * such as if there was not a new request, but it's time to look at the
 * difference between amt_wanted and amt_granted (maybe on a timer interrupt).
 *
 * Will return either the number actually granted or an error code.
 */
ssize_t core_request(struct proc *p)
{
	size_t num_granted;
	ssize_t amt_new;
	uint32_t corelist[MAX_NUM_CPUS];
	bool need_to_idle = FALSE;

	spin_lock_irqsave(&p->proc_lock);
	amt_new = p->resources[RES_CORES].amt_wanted -
	          p->resources[RES_CORES].amt_granted;
	if (amt_new < 0) {
		p->resources[RES_CORES].amt_wanted = p->resources[RES_CORES].amt_granted;
		spin_unlock_irqsave(&p->proc_lock);
		return -EINVAL;
	} else if (amt_new == 0) {
		spin_unlock_irqsave(&p->proc_lock);
		return 0;
	}
	// else, we try to handle the request

	/* TODO: someone needs to decide if the process gets the resources.
	 * we just check to see if they are available and give them out.  This
	 * should call out to the scheduler or some other *smart* function.  You
	 * could also imagine just putting it on the scheduler's queue and letting
	 * that do the core request */
	spin_lock(&idle_lock);
	if (num_idlecores >= amt_new) {
		for (int i = 0; i < amt_new; i++) {
			// grab the last one on the list
			corelist[i] = idlecoremap[num_idlecores-1];
			num_idlecores--;
		}
		num_granted = amt_new;
	} else {
		num_granted = 0;
	}
	spin_unlock(&idle_lock);
	// Now, actually give them out
	if (num_granted) {
		p->resources[RES_CORES].amt_granted += num_granted;
		switch (p->state) {
			case (PROC_RUNNING_S):
				// issue with if we're async or not (need to preempt it)
				// either of these should trip it.
				if ((current != p) || (p->vcoremap[0] != core_id()))
					panic("We don't handle async RUNNING_S core requests yet.");
				/* in the async case, we'll need to bundle vcore0's TF.  this is
				 * already done for the sync case (local syscall). */
				/* this process no longer runs on its old location (which is
				 * this core, for now, since we don't handle async calls) */
				p->vcoremap[0] = -1;
				// will need to give up this core / idle later (sync)
				need_to_idle = TRUE;
				// change to runnable_m (it's TF is already saved)
				proc_set_state(p, PROC_RUNNABLE_M);
				break;
			case (PROC_RUNNABLE_S):
				/* Issues: being on the runnable_list, proc_set_state not liking
				 * it, and not clearly thinking through how this would happen.
				 * Perhaps an async call that gets serviced after you're
				 * descheduled? */
				panic("Not supporting RUNNABLE_S -> RUNNABLE_M yet.\n");
				break;
			default:
				break;
		}
		/* give them the cores.  this will start up the extras if RUNNING_M */
		proc_give_cores(p, corelist, &num_granted);
		spin_unlock_irqsave(&p->proc_lock);
		/* if there's a race on state (like DEATH), it'll get handled by
		 * proc_run or proc_destroy */
		if (p->state == PROC_RUNNABLE_M)
			proc_run(p);
		/* if we are moving to a partitionable core from a RUNNING_S on a
		 * management core, the kernel needs to do something else on this core
		 * (just like in proc_destroy).  this cleans up the core and idles. */
		if (need_to_idle)
			abandon_core();
	} else { // nothing granted, just return
		spin_unlock_irqsave(&p->proc_lock);
	}
	return num_granted;
}

error_t resource_req(struct proc *p, int type, size_t amount, uint32_t flags)
{
	error_t retval;
	printk("Received request for type: %d, amount: %d, flag: %d\n",
	       type, amount, flags);
	if (flags & REQ_ASYNC)
		// We have no sense of time yet, or of half-filling requests
		printk("[kernel] Async requests treated synchronously for now.\n");

	/* set the desired resource amount in the process's resource list. */
	spin_lock_irqsave(&p->proc_lock);
	size_t old_amount = p->resources[type].amt_wanted;
	p->resources[type].amt_wanted = amount;
	p->resources[type].flags = flags;
	spin_unlock_irqsave(&p->proc_lock);

	// no change in the amt_wanted
	if (old_amount == amount)
		return 0;

	switch (type) {
		case RES_CORES:
			retval = core_request(p);
			// i don't like this retval hackery
			if (retval < 0)
				return retval;
			else
				return 0;
			break;
		case RES_MEMORY:
			// not clear if we should be in RUNNABLE_M or not
			printk("[kernel] Memory requests are not implemented.\n");
			return -EFAIL;
			break;
		case RES_APPLE_PIES:
			printk("You can have all the apple pies you want.\n");
			break;
		default:
			printk("[kernel] Unknown resource!  No oranges for you!\n");
			return -EINVAL;
	}
	return 0;
}

void print_resources(struct proc *p)
{
	printk("--------------------\n");
	printk("PID: %d\n", p->env_id);
	printk("--------------------\n");
	for (int i = 0; i < MAX_NUM_RESOURCES; i++)
		printk("Res type: %02d, amt wanted: %08d, amt granted: %08d\n", i,
		       p->resources[i].amt_wanted, p->resources[i].amt_granted);
}

/* TODO: change this when we get rid of the env array */
void print_all_resources(void)
{
	for (int i = 0; i < NENV; i++)
		if (envs[i].state != ENV_FREE)
			print_resources(&envs[i]);
}
