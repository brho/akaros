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

/* _S is runnable, tell the ksched to try to run it. */
void schedule_scp(struct proc *p);
/* _M exists.  Tell the ksched about it. */
void register_mcp(struct proc *p);
/* to remove from these lists, simply proc_destroy - the ksched will notice */

void schedule(void);

/* Take a look at proc's resource (temp interface) */
void poke_ksched(struct proc *p, int res_type);

/* Gets called when a pcore becomes idle (like in proc yield) */
void put_idle_core(uint32_t coreid);

/* How many vcores p will think it can have */
uint32_t max_vcores(struct proc *p);

/* P wants some cores.  Put them in pc_arr */
uint32_t proc_wants_cores(struct proc *p, uint32_t *pc_arr, uint32_t amt_new);

/* Debugging */
void sched_diag(void);
void print_idlecoremap(void);

#endif /* ROS_KERN_SCHEDULE_H */
