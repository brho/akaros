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
int proc_set_state(struct proc *p, proc_state_t state)
{
	proc_state_t curstate = p->state;
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
	 * These ought to be implemented later
	 * RBS -> D
	 * RBM -> D
	 */
	#if 1 // some sort of correctness flag
	switch (curstate) {
		case PROC_CREATED:
			if (state != PROC_RUNNABLE_S)
				panic("Invalid State Transition! PROC_CREATED to %d", state);
			break;
		case PROC_RUNNABLE_S:
			if (state != PROC_RUNNING_S && state != PROC_DYING)
				panic("Invalid State Transition! PROC_RUNNABLE_S to %d", state);
			break;
		case PROC_RUNNING_S:
			if (state != PROC_RUNNABLE_S && state != PROC_RUNNABLE_M &&
			    state != PROC_WAITING && state != PROC_DYING)
				panic("Invalid State Transition! PROC_RUNNING_S to %d", state);
			break;
		case PROC_WAITING:
			if (state != PROC_RUNNABLE_S)
				panic("Invalid State Transition! PROC_WAITING to %d", state);
			break;
		case PROC_DYING:
			if (state != PROC_CREATED) // when it is reused
				panic("Invalid State Transition! PROC_DYING to %d", state);
			break;
		case PROC_RUNNABLE_M:
			if (state != PROC_RUNNING_M && state != PROC_DYING)
				panic("Invalid State Transition! PROC_RUNNABLE_M to %d", state);
			break;
		case PROC_RUNNING_M:
			if (state != PROC_RUNNABLE_S && state != PROC_RUNNABLE_M &&
			    state != PROC_DYING)
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
