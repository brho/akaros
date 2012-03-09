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

/************** Process registration **************/
/* _S procs will get 'scheduled' every time they become RUNNABLE.  MCPs will get
 * registered on creation, and then that's it.  They will get removed from the
 * lists 'naturally' when proc_destroy() sets their state to DYING.  The ksched
 * needs to notice that, remove them from its lists, and decref. */
/* _S is runnable, tell the ksched to try to run it. */
void schedule_scp(struct proc *p);
/* _M exists.  Tell the ksched about it. */
void register_mcp(struct proc *p);

/************** Decision making **************/
/* Call the main scheduling algorithm.  Not clear yet if the main kernel will
 * ever call this directly. */
void schedule(void);

/* Proc p's resource desires changed, it recently became RUNNABLE, or something
 * in general that would lead to a new decision.  The process can directly poke
 * the ksched via a syscall, so be careful of abuse. */
void poke_ksched(struct proc *p, int res_type);

/* The calling cpu/core has nothing to do and plans to idle/halt.  This is an
 * opportunity to pick the nature of that halting (low power state, etc), or
 * provide some other work (_Ss on LL cores). */
void cpu_bored(void);

/* Gets called when a pcore becomes idle (like in proc yield).  These are 'cg'
 * cores, given to MCPs, that have been async returned to the ksched.  If the
 * ksched preempts a core, this won't get called (unless it yielded first). */
void put_idle_core(uint32_t coreid);
void put_idle_cores(uint32_t *pc_arr, uint32_t num);

/* Available resources changed (plus or minus).  Some parts of the kernel may
 * call this if a particular resource that is 'quantity-based' changes.  Things
 * like available RAM to processes, bandwidth, etc.  Cores would probably be
 * inappropriate, since we need to know which specific core is now free. */
void avail_res_changed(int res_type, long change);

/************** Proc's view of the world **************/
/* How many vcores p will think it can have */
uint32_t max_vcores(struct proc *p);

/************** Debugging **************/
void sched_diag(void);
void print_idlecoremap(void);
void print_resources(struct proc *p);
void print_all_resources(void);

#endif /* ROS_KERN_SCHEDULE_H */
