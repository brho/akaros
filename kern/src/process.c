/*
 * Copyright (c) 2009 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 */

#ifdef __SHARC__
#pragma nosharc
#endif

#include <process.h>
#include <atomic.h>
#include <smp.h>
#include <pmap.h>
#include <schedule.h>
#include <manager.h>
#include <stdio.h>
#include <assert.h>
#include <sys/queue.h>

struct proc_list proc_freelist = TAILQ_HEAD_INITIALIZER(proc_freelist);
spinlock_t freelist_lock = 0;
struct proc_list proc_runnablelist = TAILQ_HEAD_INITIALIZER(proc_runnablelist);
spinlock_t runnablelist_lock = 0;

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
 * Dispatches a process to run, either on the current core in the case of a
 * RUNNABLE_S, or on its partition in the case of a RUNNABLE_M.
 * This should never be called to "restart" a core.
 */
void proc_run(struct proc *p)
{
	spin_lock_irqsave(&p->proc_lock);
	switch (p->state) {
		case (PROC_DYING):
			spin_unlock_irqsave(&p->proc_lock);
			printk("Process %d not starting due to async death\n", p->env_id);
			// There should be no core cleanup to do (like decref).
			assert(current != p);
			// if we're a worker core, smp_idle, o/w return
			// TODO considering encapsulating this block (core_id too)
			if (core_id())
				smp_idle(); // this never returns
			return;
		case (PROC_RUNNABLE_S):
			proc_set_state(p, PROC_RUNNING_S);
			spin_unlock_irqsave(&p->proc_lock);
			// This normally doesn't return, but might error out in the future.
			proc_startcore(p, &p->env_tf);
			break;
		case (PROC_RUNNABLE_M):
			// BIG TODO: do this.
			// Check for how we're supposed to dispatch this
			// Update core map or whatever with what we're about to do

			/* There a subtle (attempted) race avoidance here.  proc_startcore
			 * can handle a death IPI, but we can't have the startcore come
			 * after the death IPI.  Otherwise, it would look like a new
			 * process.  So we hold the lock to make sure our IPI went out
			 * before a possible death IPI.  We don't IPI ourselves, since we
			 * need to let go of the lock.  This could change if we
			 * process_workqueue in the interrupt handler path and do something
			 * like light kernel threading, which ties into state bundling.
			 * Also, we may never allow proc_run to run on a target/worker core.
			 */
			// Send IPIs to everyone else involved
			spin_unlock_irqsave(&p->proc_lock);
			// if (am_involved)
			// 	proc_startcore(p, &appropriate_trapframe);
			// if not, need to make sure we don't return to the process's core 0
			panic("Unimplemented");
		default:
			spin_unlock_irqsave(&p->proc_lock);
			panic("Invalid process state in proc_run()!!");
	}
}

/*
 * Runs the given context (trapframe) of process p on the core this code
 * executes on.  The refcnt tracks how many cores have "an interest" in this
 * process, which so far just means it uses the process's page table.  See the
 * massive comments around the incref function
 *
 * Given we are RUNNING_*, an IPI for death or preemption could come in:
 * 1. death attempt (IPI to kill whatever is on your core):
 * 		we don't need to worry about protecting the stack, since we're
 * 		abandoning ship - just need to get a good cr3 and decref current, which
 * 		the death handler will do.
 * 		If a death IPI comes in, we immediately stop this function and will
 * 		never come back.
 * 2. preempt attempt (IPI to package state and maybe run something else):
 * 		- if a preempt attempt comes in while we're in the kernel, it'll
 * 		just set a flag.  we could attempt to bundle the kernel state
 * 		and rerun it later, but it's really messy (and possibly given
 * 		back to userspace).  we'll disable ints, check this flag, and if
 * 		so, handle the preemption using the same funcs as the normal
 * 		preemption handler.  nonblocking kernel calls will just slow
 * 		down the preemption while they work.  blocking kernel calls will
 * 		need to package their state properly anyway.
 *
 * TODO: in general, think about when we no longer need the stack, in case we
 * are preempted and expected to run again from somewhere else.  we can't
 * expect to have the kernel stack around anymore.  the nice thing about being
 * at this point is that we are just about ready to give up the stack anyways.
 *
 * I think we need to make it such that the kernel in "process context" never
 * gets removed from the core (displaced from its stack) without going through
 * some "bundling" code.
 */
void proc_startcore(struct proc *p, trapframe_t *tf) {
	// TODO it's possible to be DYING, but it's a rare race.  remove this soon.
	assert(p->state & (PROC_RUNNING_S | PROC_RUNNING_M));
	// sucks to have ints disabled when doing env_decref and possibly freeing
	disable_irq();
	if (per_cpu_info[core_id()].preempt_pending) {
		// TODO: handle preemption
		// the functions will need to consider deal with current like down below
		panic("Preemption not supported!");
	}
	/* If the process wasn't here, then we need to load its address space. */
	if (p != current) {
		if (proc_incref(p))
			// getting here would mean someone tried killing this while we tried
			// to start one of it's contexts (from scratch, o/w we had it's CR3
			// loaded already)
			// if this happens, the death-IPI ought to be on its way...  we can
			// either wait, or just cleanup_core() and smp_idle.
			panic("Proc is dying, handle me!"); // TODO
		lcr3(p->env_cr3);
		// we unloaded the old cr3, so decref it (if it exists)
		// TODO: Consider moving this to wherever we really "mean to leave the
		// process's context".
		if (current)
			proc_decref(current);
		current = p;
	}
	/* need to load our silly state, preferably somewhere other than here so we
	 * can avoid the case where the context was just running here.  it's not
	 * sufficient to do it in the "new process" if-block above (could be things
	 * like page faults that cause us to keep the same process, but want a
	 * different context.
	 * for now, we load this silly state here. (TODO)
	 * We also need this to be per trapframe, and not per process...
	 */
	env_pop_ancillary_state(p);
	env_pop_tf(&p->env_tf);
}

/*
 * Destroys the given process.  This may be called from another process, a light
 * kernel thread (no real process context), asynchronously/cross-core, or from
 * the process on its own core.
 *
 * Here's the way process death works:
 * 0. grab the lock (protects state transition and core map)
 * 1. set state to dying.  that keeps the kernel from doing anything for the
 * process (like proc_running it).
 * 2. figure out where the process is running (cross-core/async or RUNNING_M)
 * 3. IPI to clean up those cores (decref, etc).
 * 4. Unlock
 * 5. Clean up your core, if applicable
 * (Last core/kernel thread to decref cleans up and deallocates resources.)
 *
 * Note that some cores can be processing async calls, but will eventually
 * decref.  Should think about this more.
 */
void proc_destroy(struct proc *p)
{
	spin_lock_irqsave(&p->proc_lock);
	// Could save the state and do this outside the lock
	switch (p->state) {
		case PROC_DYING:
			return; // someone else killed this already.
		case PROC_RUNNABLE_S:
		case PROC_RUNNABLE_M:
			deschedule_proc(p);
			break;
		default:
			// Think about other lists, or better ways to do this
	}
	proc_set_state(p, PROC_DYING);
	// BIG TODO: check the coremap to find out who needs to die
	// send the death IPI to everyone else involved
	spin_unlock_irqsave(&p->proc_lock);

	proc_decref(p); // this decref is for the process in general
	atomic_dec(&num_envs);

	/*
	 * If we are currently running this address space on our core, we need a
	 * known good pgdir before releasing the old one.  This is currently the
	 * major practical implication of the kernel caring about a processes
	 * existence (the inc and decref).  This decref corresponds to the incref in
	 * proc_startcore (though it's not the only one).
	 */
	// TODO - probably make this a function, which the death IPI calls
	if (current == p) {
		lcr3(boot_cr3);
		proc_decref(p); // this decref is for the cr3
		current = NULL;
	} else {
		return;
	}

	// for old envs that die on user cores.  since env run never returns, cores
	// never get back to their old hlt/relaxed/spin state, so we need to force
	// them back to an idle function.

	if (core_id()) {
		smp_idle();
		panic("should never see me");
	}
	// else we're core 0 and can do the usual

	/* Instead of picking a new environment to run, or defaulting to the monitor
	 * like before, for now we'll hop into the manager() function, which
	 * dispatches jobs.  Note that for now we start the manager from the top,
	 * and not from where we left off the last time we called manager.  That
	 * would require us to save some context (and a stack to work on) here.
	 */
	manager();
	assert(0); // never get here
}

/*
 * The process refcnt is the number of places the process 'exists' in the
 * system.  Creation counts as 1.  Having your page tables loaded somewhere
 * (lcr3) counts as another 1.  A non-RUNNING_* process should have refcnt at
 * least 1.  If the kernel is on another core and in a processes address space
 * (like processing its backring), that counts as another 1.
 *
 * Note that the actual loading and unloading of cr3 is up to the caller, since
 * that's not the only use for this (and decoupling is more flexible).
 *
 * The refcnt should always be greater than 0 for processes that aren't dying.
 * When refcnt is 0, the process is dying and should not allow any more increfs.
 * A process can be dying with a refcnt greater than 0, since it could be
 * waiting for other cores to "get the message" to die, or a kernel core can be
 * finishing work in the processes's address space.
 *
 * Implementation aside, the important thing is that we atomically increment
 * only if it wasn't already 0.  If it was 0, then we shouldn't be attaching to
 * the process, so we return an error, which should be handled however is
 * appropriate.  We currently use spinlocks, but some sort of clever atomics
 * would work too.
 *
 * Also, no one should ever update the refcnt outside of these functions.
 * Eventually, we'll have Ivy support for this. (TODO)
 */
error_t proc_incref(struct proc *p)
{
	error_t retval = 0;
	spin_lock_irqsave(&p->proc_lock);
	if (p->env_refcnt)
		p->env_refcnt++;
	else
		retval = -EBADENV;
	spin_unlock_irqsave(&p->proc_lock);
	return retval;
}

/*
 * When the kernel is done with a process, it decrements its reference count.
 * When the count hits 0, no one is using it and it should be freed.
 * "Last one out" actually finalizes the death of the process.  This is tightly
 * coupled with the previous function (incref)
 * Be sure to load a different cr3 before calling this!
 */
void proc_decref(struct proc *p)
{
	spin_lock_irqsave(&p->proc_lock);
	p->env_refcnt--;
	spin_unlock_irqsave(&p->proc_lock);
	// if we hit 0, no one else will increment and we can check outside the lock
	if (p->env_refcnt == 0)
		env_free(p);
}
