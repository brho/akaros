/*
 * Copyright (c) 2009 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Scheduling and dispatching.
 */

#ifndef ROS_KERN_SCHEDULE_H
#define ROS_KERN_SCHEDULE_H

#include <process.h>

void schedule_init(void);

/*
 * Add a process to the runnable list.  If you do this, do not manually env_run
 * the process.  It must be selected with schedule().  This also applies to
 * smp_call_function to indirectly call env_run.  Currently, this is always
 * called with changing the state to RUNNABLE_S, but in the future, it might
 * need a RUNNABLE_M instead - but one or the other should be done before
 * calling this.
 */
void schedule_proc(struct proc *p);

/* Rip a process out of the runnable list */
void deschedule_proc(struct proc *p);

/* Pick and run a process.  Note that this can return. */
void schedule(void);

/* Debugging */
void dump_proclist(struct proc_list *list);

#endif // !ROS_KERN_SCHEDULE_H
