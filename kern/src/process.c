/*
 * Copyright (c) 2009 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 */

#ifdef __SHARC__
#pragma nosharc
#endif

#include <arch/arch.h>
#include <process.h>
#include <atomic.h>
#include <smp.h>
#include <pmap.h>
#include <schedule.h>
#include <manager.h>
#include <stdio.h>
#include <assert.h>
#include <timing.h>
#include <sys/queue.h>

/* Process Lists */
struct proc_list proc_freelist = TAILQ_HEAD_INITIALIZER(proc_freelist);
spinlock_t freelist_lock = 0;
struct proc_list proc_runnablelist = TAILQ_HEAD_INITIALIZER(proc_runnablelist);
spinlock_t runnablelist_lock = 0;

/* Tracks which cores are idle, similar to the vcoremap.  Each value is the
 * physical coreid of an unallocated core. */
spinlock_t idle_lock = 0;
uint32_t LCKD(&idle_lock) (RO idlecoremap)[MAX_NUM_CPUS];
uint32_t LCKD(&idle_lock) num_idlecores = 0;

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
			if (!management_core())
				smp_idle(); // this never returns
			return;
		case (PROC_RUNNABLE_S):
			proc_set_state(p, PROC_RUNNING_S);
			// We will want to know where this process is running, even if it is
			// only in RUNNING_S.  can use the vcoremap, which makes death easy.
			// we may need the pcoremap entry to mark it as a RUNNING_S core, or
			// else update it here. (TODO) (PCORE)
			p->num_vcores = 0;
			p->vcoremap[0] = core_id();
			spin_unlock_irqsave(&p->proc_lock);
			// This normally doesn't return, but might error out in the future.
			proc_startcore(p, &p->env_tf);
			break;
		case (PROC_RUNNABLE_M):
			proc_set_state(p, PROC_RUNNING_M);
			/* vcoremap[i] holds the coreid of the physical core allocated to
			 * this process.  It is set outside proc_run.  For the active
			 * message, a0 = struct proc*, a1 = struct trapframe*.   */
			if (p->num_vcores) {
				// TODO: handle silly state (HSS)
				// set virtual core 0 to run the main context
#ifdef __IVY__
				send_active_msg_sync(p->vcoremap[0], __startcore, p,
				                     &p->env_tf, (void *SNT)0);
#else
				send_active_msg_sync(p->vcoremap[0], (void *)__startcore,
				                     (void *)p, (void *)&p->env_tf, 0);
#endif
				/* handle the others.  note the sync message will spin until
				 * there is a free active message slot, which could lock up the
				 * system.  think about this. (TODO) */
				for (int i = 1; i < p->num_vcores; i++)
#ifdef __IVY__
					send_active_msg_sync(p->vcoremap[i], __startcore,
					                     p, (trapframe_t *CT(1))NULL, (void *SNT)i);
#else
					send_active_msg_sync(p->vcoremap[i], (void *)__startcore,
					                     (void *)p, (void *)0, (void *)i);
#endif
			}
			/* There a subtle (attempted) race avoidance here.  proc_startcore
			 * can handle a death message, but we can't have the startcore come
			 * after the death message.  Otherwise, it would look like a new
			 * process.  So we hold the lock to make sure our message went out
			 * before a possible death message.
			 * - Likewise, we need interrupts to be disabled, in case one of the
			 *   messages was for us, and reenable them after letting go of the
			 *   lock.  This is done by spin_lock_irqsave, so be careful if you
			 *   change this.
			 * - This can also be done far more intelligently / efficiently,
			 *   like skipping in case it's busy and coming back later.
			 * - Note there is no guarantee this core's interrupts were on, so
			 *   it may not get the message for a while... */
			spin_unlock_irqsave(&p->proc_lock);
			break;
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
	// it's possible to be DYING, but it's a rare race.
	//if (p->state & (PROC_RUNNING_S | PROC_RUNNING_M))
	//	printk("dying before (re)startcore on core %d\n", core_id());

	// sucks to have ints disabled when doing env_decref and possibly freeing
	disable_irq();
	if (per_cpu_info[core_id()].preempt_pending) {
		// TODO: handle preemption
		// the functions will need to consider deal with current like down below
		panic("Preemption not supported!");
	}
	/* If the process wasn't here, then we need to load its address space. */
	if (p != current) {
		if (proc_incref(p)) {
			// getting here would mean someone tried killing this while we tried
			// to start one of it's contexts (from scratch, o/w we had it's CR3
			// loaded already)
			// if this happens, a no-op death-IPI ought to be on its way...  we can
			// just smp_idle()
			smp_idle();
		}
		lcr3(p->env_cr3);
		// we unloaded the old cr3, so decref it (if it exists)
		// TODO: Consider moving this to wherever we really "mean to leave the
		// process's context".
		if (current)
			proc_decref(current);
		set_cpu_curenv(p);
	}
	/* need to load our silly state, preferably somewhere other than here so we
	 * can avoid the case where the context was just running here.  it's not
	 * sufficient to do it in the "new process" if-block above (could be things
	 * like page faults that cause us to keep the same process, but want a
	 * different context.
	 * for now, we load this silly state here. (TODO) (HSS)
	 * We also need this to be per trapframe, and not per process...
	 */
	env_pop_ancillary_state(p);
	env_pop_tf(tf);
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
	// Note this code relies on this lock disabling interrupts, similar to
	// proc_run.
	spin_lock_irqsave(&p->proc_lock);
	switch (p->state) {
		case PROC_DYING:
			return; // someone else killed this already.
		case PROC_RUNNABLE_S:
		case PROC_RUNNABLE_M:
			// Think about other lists, like WAITING, or better ways to do this
			deschedule_proc(p);
			break;
		case PROC_RUNNING_S:
			#if 0
			// here's how to do it manually
			if (current == p) {
				lcr3(boot_cr3);
				proc_decref(p); // this decref is for the cr3
				current = NULL;
			}
			#endif
			send_active_msg_sync(p->vcoremap[0], __death, (void *SNT)0,
			                     (void *SNT)0, (void *SNT)0);
			#if 0
			/* right now, RUNNING_S only runs on a mgmt core (0), not cores
			 * managed by the idlecoremap.  so don't do this yet. */
			spin_lock(&idle_lock);
			idlecoremap[num_idlecores++] = p->vcoremap[0];
			spin_unlock(&idle_lock);
			#endif
			break;
		case PROC_RUNNING_M:
			/* Send the DEATH message to every core running this process, and
			 * deallocate the cores.
			 * The rule is that the vcoremap is set before proc_run, and reset
			 * within proc_destroy */
			spin_lock(&idle_lock);
			for (int i = 0; i < p->num_vcores; i++) {
				send_active_msg_sync(p->vcoremap[i], __death, (void *SNT)0,
				                     (void *SNT)0, (void *SNT)0);
				// give the pcore back to the idlecoremap
				assert(num_idlecores < num_cpus); // sanity
				idlecoremap[num_idlecores++] = p->vcoremap[i];
				p->vcoremap[i] = 0; // TODO: might need a better signal
			}
			spin_unlock(&idle_lock);
			break;
		default:
			// TODO: getting here if it's already dead and free (ENV_FREE).
			// Need to sort reusing process structures and having pointers to
			// them floating around the system.
			panic("Weird state(0x%08x) in proc_destroy", p->state);
	}
	proc_set_state(p, PROC_DYING);

	atomic_dec(&num_envs);
	/* TODO: (REF) dirty hack.  decref currently uses a lock, but needs to use
	 * CAS instead (another lock would be slightly less ghetto).  but we need to
	 * decref before releasing the lock, since that could enable interrupts,
	 * which would have us receive the DEATH IPI if this was called locally by
	 * the target process. */
	//proc_decref(p); // this decref is for the process in general
	p->env_refcnt--;
	size_t refcnt = p->env_refcnt; // need to copy this in so it's not reloaded

	/* After unlocking, we can receive a DEATH IPI and clean up */
	spin_unlock_irqsave(&p->proc_lock);

	// coupled with the refcnt-- above, from decref.  if this happened,
	// proc_destroy was called "remotely", and with no one else refcnting
	if (!refcnt)
		env_free(p);

	/* If we were running the process, we should have received an IPI by now.
	 * If not, odds are interrupts are disabled, which shouldn't happen while
	 * servicing syscalls. */
	assert(current != p);
	return;
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
 *
 * TODO: (REF) change to use CAS.
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
 *
 * TODO: (REF) change to use CAS.  Note that when we do so, we may be holding
 * the process lock when calling env_free().
 */
void proc_decref(struct proc *p)
{
	spin_lock_irqsave(&p->proc_lock);
	p->env_refcnt--;
	size_t refcnt = p->env_refcnt; // need to copy this in so it's not reloaded
	spin_unlock_irqsave(&p->proc_lock);
	// if we hit 0, no one else will increment and we can check outside the lock
	if (!refcnt)
		env_free(p);
}

/* Active message handler to start a process's context on this core.  Tightly
 * coupled with proc_run() */
#ifdef __IVY__
void __startcore(trapframe_t *tf, uint32_t srcid, struct proc *CT(1) a0,
                 trapframe_t *CT(1) a1, void *SNT a2)
#else
void __startcore(trapframe_t *tf, uint32_t srcid, void * a0, void * a1,
                 void * a2)
#endif
{
	uint32_t coreid = core_id();
	struct proc *p_to_run = (struct proc *CT(1))a0;
	trapframe_t local_tf;
	trapframe_t *tf_to_pop = (trapframe_t *CT(1))a1;

	printk("Startcore on core %d\n", coreid);
	assert(p_to_run);
	// TODO: handle silly state (HSS)
	if (!tf_to_pop) {
		tf_to_pop = &local_tf;
		memset(tf_to_pop, 0, sizeof(*tf_to_pop));
		proc_init_trapframe(tf_to_pop);
		// Note the init_tf sets tf_to_pop->tf_esp = USTACKTOP;
		proc_set_tfcoreid(tf_to_pop, (uint32_t)a2);
		proc_set_program_counter(tf_to_pop, p_to_run->env_entry);
	}
	proc_startcore(p_to_run, tf_to_pop);
}

/* Active message handler to stop running whatever context is on this core and
 * to idle.  Note this leaves no trace of what was running.
 * It's okay if death comes to a core that's already idling and has no current.
 * It could happen if a process decref'd before proc_startcore could incref. */
void __death(trapframe_t *tf, uint32_t srcid, void *SNT a0, void *SNT a1,
             void *SNT a2)
{
	/* If we are currently running an address space on our core, we need a known
	 * good pgdir before releasing the old one.  This is currently the major
	 * practical implication of the kernel caring about a processes existence
	 * (the inc and decref).  This decref corresponds to the incref in
	 * proc_startcore (though it's not the only one). */
	if (current) {
		lcr3(boot_cr3);
		proc_decref(current);
		set_cpu_curenv(NULL);
	}
	smp_idle();
}

void print_idlecoremap(void)
{
	spin_lock(&idle_lock);
	for (int i = 0; i < num_idlecores; i++)
		printk("idlecoremap[%d] = %d\n", i, idlecoremap[i]);
	spin_unlock(&idle_lock);
}
