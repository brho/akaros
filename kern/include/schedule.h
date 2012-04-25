/*
 * Copyright (c) 2009 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Scheduling and dispatching.
 */

#ifndef ROS_KERN_SCHEDULE_H
#define ROS_KERN_SCHEDULE_H

#include <ros/common.h>
#include <sys/queue.h>

struct proc;	/* process.h includes us, but we need pointers now */
TAILQ_HEAD(proc_list, proc);		/* Declares 'struct proc_list' */

/* One of these embedded in every struct proc */
struct sched_proc_data {
	TAILQ_ENTRY(proc)			proc_link;			/* tailq linkage */
	struct proc_list 			*cur_list;			/* which tailq we're on */
};

void schedule_init(void);

/************** Process management **************/
/* Tell the ksched about the process, which it will track cradle-to-grave */
void register_proc(struct proc *p);

/* Makes sure p is runnable.  Callers include event delivery, SCP yield, and new
 * SCPs.  Will trigger the __sched_.cp_wakeup() callbacks. */
void proc_wakeup(struct proc *p);

/* The ksched starts the death process (lock ordering issue), which calls back
 * to proc.c's __proc_destroy while holding the locks (or whatever) */
void proc_destroy(struct proc *p);

/* Changes the proc from an SCP to an MCP */
int proc_change_to_m(struct proc *p);

/************** Decision making **************/
/* Call the main scheduling algorithm.  Not clear yet if the main kernel will
 * ever call this directly. */
void schedule(void);

/* Proc p's resource desires changed, or something in general that would lead to
 * a new decision.  The process can directly poke the ksched via a syscall, so
 * be careful of abuse. */
void poke_ksched(struct proc *p, int res_type);

/* Callbacks triggered from proc_wakeup() */
void __sched_mcp_wakeup(struct proc *p);
void __sched_scp_wakeup(struct proc *p);

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
