/*
 * Copyright (c) 2009 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * All things processes!  As we move away from the old envs to processes,
 * we'll move things into here that are designed for multicore processes.
 */

#ifndef ROS_KERN_PROCESS_H
#define ROS_KERN_PROCESS_H

/* Process States.  Not 100% on the names yet. */
typedef enum {
	ENV_FREE, // TODO don't use this shit for process allocation flagging
	PROC_CREATED,
	PROC_RUNNABLE_S,
	PROC_RUNNING_S,
	PROC_WAITING,             // can split out to INT and UINT
	PROC_DYING,
	PROC_RUNNABLE_M,          // ready, just needs all of its resources (cores)
	PROC_RUNNING_M            // running, manycore style
} proc_state_t;

#include <env.h>

// Till we remove the old struct Env
#define proc Env

int proc_set_state(struct proc *p, proc_state_t state) WRITES(p->state);
struct proc *get_proc(unsigned pid);
bool proc_controls(struct proc *actor, struct proc *target);

#endif // !ROS_KERN_PROCESS_H
