/*
 * Copyright (c) 2009 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 */

#include <process.h>
#include <atomic.h>
#include <assert.h>

/*
 * While this could be done with just an assignment, this gives us the
 * opportunity to check for bad transitions.  Might compile these out later, so
 * we shouldn't rely on them for sanity checking from userspace.
 */
int proc_set_state(struct proc *p, uint32_t state)
{
	uint32_t curstate = p->state;
	/* Valid transitions:
	 * C   -> RBS
	 * RBS -> RGS
	 * RGS -> RBS
	 * RGS -> W
	 * W   -> RBS
	 * RGS -> RBM
	 * RBM -> RGM
	 * RGM -> RBM
	 * RGM -> RBS
	 * RGS -> D
	 * RGM -> D
	 * 
	 * These ought to be implemented later (allowed, not thought through yet).
	 * RBS -> D
	 * RBM -> D
	 *
	 * This isn't allowed yet, may be later.
	 * C   -> D
	 */
	#if 1 // some sort of correctness flag
	switch (curstate) {
		case PROC_CREATED:
			if (state != PROC_RUNNABLE_S)
				panic("Invalid State Transition! PROC_CREATED to %d", state);
			break;
		case PROC_RUNNABLE_S:
			if (!(state & (PROC_RUNNING_S | PROC_DYING)))
				panic("Invalid State Transition! PROC_RUNNABLE_S to %d", state);
			break;
		case PROC_RUNNING_S:
			if (!(state & (PROC_RUNNABLE_S | PROC_RUNNABLE_M | PROC_WAITING |
			               PROC_DYING)))
				panic("Invalid State Transition! PROC_RUNNING_S to %d", state);
			break;
		case PROC_WAITING:
			if (state != PROC_RUNNABLE_S)
				panic("Invalid State Transition! PROC_WAITING to %d", state);
			break;
		case PROC_DYING:
			if (state != PROC_CREATED) // when it is reused (TODO)
				panic("Invalid State Transition! PROC_DYING to %d", state);
			break;
		case PROC_RUNNABLE_M:
			if (!(state & (PROC_RUNNING_M | PROC_DYING)))
				panic("Invalid State Transition! PROC_RUNNABLE_M to %d", state);
			break;
		case PROC_RUNNING_M:
			if (!(state & (PROC_RUNNABLE_S | PROC_RUNNABLE_M | PROC_DYING)))
				panic("Invalid State Transition! PROC_RUNNING_M to %d", state);
			break;
	}
	#endif
	p->state = state;
	return 0;
}

/* Change this when we aren't using an array */
struct proc *get_proc(unsigned pid)
{
	// should have some error checking when we do this for real
	return &envs[ENVX(pid)];
}

/* Whether or not actor can control target */
bool proc_controls(struct proc *actor, struct proc *target)
{
	return target->env_parent_id == actor->env_id;
}

/*
 * Runs the given context (trapframe) of process p on the core this code
 * executes on.  The refcnt tracks how many cores have "an interest" in this
 * process, which so far just means it uses the process's page table.  See the
 * massive comments around the incref function
 *
 * TODO: think about how an interrupt could abort this, esp when we want to
 * destroy it.  need a way to not lose the kernel stack.  For example, we could
 * receive an IPI that tells us to preempt this process (or even kill it) and
 * run something different.
 * TODO: in general, think about when we no longer need the stack, in case we
 * are preempted and expected to run again from somewhere else.  we can't
 * expect to have the kernel stack around anymore.
 *
 * I think we need to make it such that the kernel in "process context"
 * never gets removed from the core (displaced from its stack)
 * would like to leave interrupts on too, so long as we come back.
 * Consider a moveable flag or something.
 *
 * Perhaps we could have a workqueue with the todo item put there by the
 * interrupt handler when it realizes we were in the kernel in the first place.
 * disable ints before checking the queue and deciding to pop out or whatever to
 * ensure atomicity.
 */
void proc_startcore(struct proc *p, trapframe_t *tf) {
	/*
	 * TODO: okay, we have this.  now handle scenarios based on these
	 * assumptions (transitions from these states) like:
	 * 		death attempt
	 * 		preempt attempt
	 */
	assert(p->state & (PROC_RUNNING_S | PROC_RUNNING_M));
	/* If the process wasn't here, then we need to load its address space. */
	if (p != current) {
		if (env_incref(p))
			// getting here would mean someone tried killing this while we tried
			// to start one of it's contexts (from scratch, o/w we had it's CR3
			// loaded already)
			panic("Proc is dying, handle me!"); lcr3(p->env_cr3);
		// we unloaded the old cr3, so decref it (if it exists)
		// TODO: Consider moving this to wherever we really "mean to leave the
		// process's context".
		if (current)
			env_decref(current);
		current = p;
		/* also need to load our silly state, though this implies it's the same
		 * context, and not just the same process
		 * TODO: this is probably a lie, think about page faults
		 */
		// load_our_silly_state();
	}
	/* If the process entered the kernel via sysenter, we need to leave via
	 * sysexit.  sysenter trapframes have 0 for a CS, which is pushed in
	 * sysenter_handler.
	 */
	if (tf->tf_cs)
  		env_pop_tf(tf);
	else
		env_pop_tf_sysexit(tf);
}
