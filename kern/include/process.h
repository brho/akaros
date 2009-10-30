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

#include <ros/common.h>
#include <trap.h>
#include <atomic.h>

/* Process States.  Not 100% on the names yet.  RUNNABLE_* are waiting to go to
 * RUNNING_*.  For instance, RUNNABLE_M is expecting to go to RUNNING_M.  It
 * could be waiting for it's timeslice, or possibly for all the cores it asked
 * for.  You use proc_run() to transition between these states.
 *
 * Difference between the _M and the _S states:
 * - _S : legacy process mode
 * - RUNNING_M implies *guaranteed* core(s).  You can be a single core in the
 *   RUNNING_M state.  The guarantee is subject to time slicing, but when you
 *   run, you get all of your cores.
 * - The time slicing is at a coarser granularity for _M states.  This means
 *   that when you run an _S on a core, it should be interrupted/time sliced
 *   more often, which also means the core should be classified differently for
 *   a while.  Possibly even using it's local APIC timer.
 * - A process in an _M state will be informed about changes to its state, e.g.,
 *   will have a handler run in the event of a page fault
 */

#define PROC_CREATED			0x01
#define PROC_RUNNABLE_S			0x02
#define PROC_RUNNING_S			0x04
#define PROC_WAITING			0x08 // can split out to INT and UINT
#define PROC_DYING				0x10
#define PROC_RUNNABLE_M			0x20
#define PROC_RUNNING_M			0x40
// TODO don't use this shit for process allocation flagging
#define ENV_FREE				0x80

#include <env.h>

// Till we remove the old struct Env
#define proc Env

TAILQ_HEAD(proc_list, proc);		// Declares 'struct proc_list'

extern spinlock_t freelist_lock;
extern struct proc_list LCKD(&freelist_lock)proc_freelist;

extern spinlock_t runnablelist_lock;
extern struct proc_list LCKD(&runnablelist_lock) proc_runnablelist;

/* Idle cores: ones able to be exclusively given to a process (worker cores). */
extern spinlock_t idle_lock;  // never grab this before a proc_lock
extern uint32_t LCKD(&idle_lock) (RO idlecoremap)[MAX_NUM_CPUS];
extern uint32_t LCKD(&idle_lock) num_idlecores;

/* Process management: */
int proc_set_state(struct proc *p, uint32_t state) WRITES(p->state);
struct proc *get_proc(unsigned pid);
bool proc_controls(struct proc *SAFE actor, struct proc *SAFE target);
/* Transition from RUNNABLE_* to RUNNING_*. */
void proc_run(struct proc *SAFE p);
void proc_startcore(struct proc *SAFE p, trapframe_t *SAFE tf)
     __attribute__((noreturn));
void proc_destroy(struct proc *SAFE p);
void proc_yield(struct proc *SAFE p);

/* Process core management.  Only call these if you are RUNNING_M or RUNNABLE_M.
 * These all adjust the vcoremap and take appropriate actions (like __startcore
 * if you were already RUNNING_M.  You could be RUNNABLE_M with no vcores when
 * these are done (basically preempted, and waiting to get run again).
 * All of these could modify corelist and *num to communicate info back out,
 * which would be the list of cores that are known to be free.
 *
 * WARNING: YOU MUST HOLD THE PROC_LOCK BEFORE CALLING THESE! */
/* Gives process p the additional num cores listed in corelist */
error_t proc_give_cores(struct proc *SAFE p, uint32_t corelist[], size_t *num);
/* Makes process p's coremap look like corelist (add, remove, etc) */
error_t proc_set_allcores(struct proc *SAFE p, uint32_t corelist[], size_t *num,
                          amr_t message,TV(a0t) arg0, TV(a1t) arg1, TV(a2t) arg2);
/* Takes from process p the num cores listed in corelist */
error_t proc_take_cores(struct proc *SAFE p, uint32_t corelist[], size_t *num,
                        amr_t message, TV(a0t) arg0, TV(a1t) arg1, TV(a2t) arg2);
error_t proc_take_allcores(struct proc *SAFE p, amr_t message, TV(a0t) arg0,
                           TV(a1t) arg1, TV(a2t) arg2);

/* The reference counts are mostly to track how many cores loaded the cr3 */
error_t proc_incref(struct proc *SAFE p);
void proc_decref(struct proc *SAFE p);

/* Allows the kernel to figure out what process is running on this core.  Can be
 * used just like a pointer to a struct proc.  Need these to be macros due to
 * some circular dependencies with smp.h. */
#include <smp.h>
#define current per_cpu_info[core_id()].cur_proc
#define set_current_proc(p) per_cpu_info[core_id()].cur_proc = (p)

/* Allows the kernel to figure out what tf is on this core's stack.  Can be used
 * just like a pointer to a struct Trapframe.  Need these to be macros due to
 * some circular dependencies with smp.h.  This is done here instead of
 * elsewhere (like trap.h) for other elliptical reasons. */
#define current_tf per_cpu_info[core_id()].cur_tf
#define set_current_tf(tf) per_cpu_info[core_id()].cur_tf = (tf)

void abandon_core(void);

/* Active message handlers for process management */
#ifdef __IVY__
void __startcore(trapframe_t *tf, uint32_t srcid, struct proc *CT(1) a0,
                 trapframe_t *CT(1) a1, void *SNT a2);
void __death(trapframe_t *tf, uint32_t srcid, void *SNT a0, void *SNT a1,
             void *SNT a2);
#else
void __startcore(trapframe_t *tf, uint32_t srcid, void * a0, void * a1,
                 void * a2);
void __death(trapframe_t *tf, uint32_t srcid, void * a0, void * a1,
             void * a2);
#endif

/* Arch Specific */
void proc_set_program_counter(trapframe_t *SAFE tf, uintptr_t pc);
void proc_init_trapframe(trapframe_t *SAFE tf);
void proc_set_tfcoreid(trapframe_t *SAFE tf, uint32_t id);
void proc_set_syscall_retval(trapframe_t *SAFE tf, intreg_t value);

/* Degubbing */
void print_idlecoremap(void);
void print_proc_info(pid_t pid);

#endif // !ROS_KERN_PROCESS_H
