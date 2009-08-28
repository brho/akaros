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

#include <arch/types.h>
#include <atomic.h>

/* Process States.  Not 100% on the names yet. */
#define PROC_CREATED			0x01
#define PROC_RUNNABLE_S			0x02
#define PROC_RUNNING_S			0x04
#define PROC_WAITING			0x08  // can split out to INT and UINT
#define PROC_DYING				0x10
#define PROC_RUNNABLE_M			0x20 // ready, needs all of its resources (cores)
#define PROC_RUNNING_M			0x40 // running, manycore style
// TODO don't use this shit for process allocation flagging
#define ENV_FREE				0x80

#include <env.h>

// Till we remove the old struct Env
#define proc Env

TAILQ_HEAD(proc_list, proc);		// Declares 'struct proc_list'
extern struct proc_list proc_freelist;
extern spinlock_t freelist_lock;
extern struct proc_list proc_runnablelist;
extern spinlock_t runnablelist_lock;

int proc_set_state(struct proc *p, uint32_t state) WRITES(p->state);
struct proc *get_proc(unsigned pid);
bool proc_controls(struct proc *actor, struct proc *target);
void proc_run(struct proc *p);
void (proc_startcore)(struct proc *p, trapframe_t *tf) __attribute__((noreturn));
void (proc_destroy)(struct proc *SAFE p);

/* The reference counts are mostly to track how many cores loaded the cr3 */
error_t proc_incref(struct proc *SAFE p);
void proc_decref(struct proc *SAFE p);
#endif // !ROS_KERN_PROCESS_H
