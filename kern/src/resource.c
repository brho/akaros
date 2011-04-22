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
 *
 * TODO: this is a giant function.  need to split it up a bit, probably move the
 * guts to process.c and have functions to call for the brains.
 */
ssize_t core_request(struct proc *p)
{
	size_t num_granted;
	ssize_t amt_new;
	uint32_t corelist[MAX_NUM_CPUS];
	bool need_to_idle = FALSE;
	bool self_ipi_pending = FALSE;

	/* There are a few things broken for now if you don't have a current_tf */
	assert(current_tf);
	spin_lock(&p->proc_lock);
	if (p->state == PROC_DYING) {
		spin_unlock(&p->proc_lock);
		return -EFAIL;
	}
	/* check to see if this is a full deallocation.  for cores, it's a
	 * transition from _M to _S.  Will be issues with handling this async. */
	if (!p->resources[RES_CORES].amt_wanted) {
		assert(p->state == PROC_RUNNING_M); // TODO: (ACR) async core req
		/* save the context, to be restarted in _S mode */
		p->env_tf = *current_tf;
		current_tf = 0;			/* Make sure it isn't used in the future */
		env_push_ancillary_state(p); // TODO: (HSS)
		/* sending death, since it's not our job to save contexts or anything in
		 * this case.  also, if this returns true, we will not return down
		 * below, and need to eat the reference to p */
		self_ipi_pending = __proc_take_allcores(p, __death, 0, 0, 0);
		__proc_set_state(p, PROC_RUNNABLE_S);
		schedule_proc(p);
		spin_unlock(&p->proc_lock);
		__proc_kmsg_pending(p, self_ipi_pending);
		return 0;
	}
	/* Fail if we can never handle this amount (based on how many we told the
	 * process it can get). */
	if (p->resources[RES_CORES].amt_wanted > p->procinfo->max_vcores) {
		spin_unlock(&p->proc_lock);
		return -EFAIL;
	}
	/* otherwise, see how many new cores are wanted */
	amt_new = p->resources[RES_CORES].amt_wanted -
	          p->resources[RES_CORES].amt_granted;
	if (amt_new < 0) {
		p->resources[RES_CORES].amt_wanted = p->resources[RES_CORES].amt_granted;
		spin_unlock(&p->proc_lock);
		return -EINVAL;
	} else if (amt_new == 0) {
		spin_unlock(&p->proc_lock);
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
		/* In this case, you might want to preempt or do other fun things... */
		num_granted = 0;
	}
	spin_unlock(&idle_lock);

	// Now, actually give them out
	if (num_granted) {
		switch (p->state) {
			case (PROC_RUNNING_S):
				// issue with if we're async or not (need to preempt it)
				// either of these should trip it. TODO: (ACR) async core req
				// TODO: relies on vcore0 being the caller (VC#)
				// TODO: do this in process.c and use this line:
				//if ((current != p) || (get_pcoreid(p, 0) != core_id()))
				if ((current != p) || (p->procinfo->vcoremap[0].pcoreid != core_id()))
					panic("We don't handle async RUNNING_S core requests yet.");
				/* save the tf so userspace can restart it.  Like in __notify,
				 * this assumes a user tf is the same as a kernel tf.  We save
				 * it in the preempt slot so that we can also save the silly
				 * state. */
				struct preempt_data *vcpd = &p->procdata->vcore_preempt_data[0];
				vcpd->preempt_tf = *current_tf;
				save_fp_state(&vcpd->preempt_anc);
				__seq_start_write(&vcpd->preempt_tf_valid);
				/* If we remove this, vcore0 will start where the _S left off */
				vcpd->notif_pending = TRUE;
				assert(vcpd->notif_enabled);
				/* in the async case, we'll need to remotely stop and bundle
				 * vcore0's TF.  this is already done for the sync case (local
				 * syscall). */
				/* this process no longer runs on its old location (which is
				 * this core, for now, since we don't handle async calls) */
				__seq_start_write(&p->procinfo->coremap_seqctr);
				// TODO: (VC#) might need to adjust num_vcores
				// TODO: (ACR) will need to unmap remotely (receive-side)
				__unmap_vcore(p, 0);
				__seq_end_write(&p->procinfo->coremap_seqctr);
				// will need to give up this core / idle later (sync)
				need_to_idle = TRUE;
				// change to runnable_m (it's TF is already saved)
				__proc_set_state(p, PROC_RUNNABLE_M);
				break;
			case (PROC_RUNNABLE_S):
				/* Issues: being on the runnable_list, proc_set_state not liking
				 * it, and not clearly thinking through how this would happen.
				 * Perhaps an async call that gets serviced after you're
				 * descheduled? */
				panic("Not supporting RUNNABLE_S -> RUNNABLE_M yet.\n");
				break;
			case (PROC_DYING):
				warn("Dying, core request coming from %d\n", core_id());
			default:
				break;
		}
		/* give them the cores.  this will start up the extras if RUNNING_M. */
		self_ipi_pending = __proc_give_cores(p, corelist, num_granted);
		spin_unlock(&p->proc_lock);
		// TODO: (RMS) think about this, esp when its called from a scheduler
		__proc_kmsg_pending(p, self_ipi_pending);
		/* if there's a race on state (like DEATH), it'll get handled by
		 * proc_run or proc_destroy */
		if (p->state == PROC_RUNNABLE_M)
			proc_run(p);
		/* if we are moving to a partitionable core from a RUNNING_S on a
		 * management core, the kernel needs to do something else on this core
		 * (just like in proc_destroy).  it also needs to decref, to consume the
		 * reference that came into this function (since we don't return).  */
		if (need_to_idle) {
			proc_decref(p);
			abandon_core();
			smp_idle();
		}
	} else { // nothing granted, just return
		spin_unlock(&p->proc_lock);
	}
	return num_granted;
}

error_t resource_req(struct proc *p, int type, size_t amt_wanted,
                     size_t amt_wanted_min, uint32_t flags)
{
	error_t retval;
	printd("Received request for type: %d, amt_wanted: %d, amt_wanted_min: %d, "
	       "flag: %d\n", type, amt_wanted, amt_wanted_min, flags);
	if (flags & REQ_ASYNC)
		// We have no sense of time yet, or of half-filling requests
		printk("[kernel] Async requests treated synchronously for now.\n");

	/* set the desired resource amount in the process's resource list. */
	spin_lock(&p->proc_lock);
	size_t old_amount = p->resources[type].amt_wanted;
	p->resources[type].amt_wanted = amt_wanted;
	p->resources[type].amt_wanted_min = MIN(amt_wanted_min, amt_wanted);
	p->resources[type].flags = flags;
	spin_unlock(&p->proc_lock);

	switch (type) {
		case RES_CORES:
			retval = core_request(p);
			// i don't like this retval hackery
			if (retval < 0) {
				set_errno(-retval);
				return -1;
			}
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
	printk("PID: %d\n", p->pid);
	printk("--------------------\n");
	for (int i = 0; i < MAX_NUM_RESOURCES; i++)
		printk("Res type: %02d, amt wanted: %08d, amt granted: %08d\n", i,
		       p->resources[i].amt_wanted, p->resources[i].amt_granted);
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
