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

#define procstate2str(state) ((state)==PROC_CREATED    ? "CREATED   " : \
                              (state)==PROC_RUNNABLE_S ? "RUNNABLE_S" : \
                              (state)==PROC_RUNNING_S  ? "RUNNING_S " : \
                              (state)==PROC_WAITING    ? "WAITING   " : \
                              (state)==PROC_DYING      ? "DYING     " : \
                              (state)==PROC_RUNNABLE_M ? "RUNNABLE_M" : \
                              (state)==PROC_RUNNING_M  ? "RUNNING_M " : \
                                                         "UNKNOWN   ")

#include <env.h>

// Till we remove the old struct Env
#define proc Env

TAILQ_HEAD(proc_list, proc);		// Declares 'struct proc_list'

extern spinlock_t runnablelist_lock;
extern struct proc_list LCKD(&runnablelist_lock) proc_runnablelist;

/* Can use a htable iterator to iterate through all active procs */
extern struct hashtable *pid_hash;
extern spinlock_t pid_hash_lock;

/* Idle cores: ones able to be exclusively given to a process (worker cores). */
extern spinlock_t idle_lock;  // never grab this before a proc_lock
extern uint32_t LCKD(&idle_lock) (RO idlecoremap)[MAX_NUM_CPUS];
extern uint32_t LCKD(&idle_lock) num_idlecores;

/* Initialization */
void proc_init(void);
void proc_init_procinfo(struct proc *p);

/* Process management: */
struct proc *proc_create(uint8_t *COUNT(size) binary, size_t size);
int __proc_set_state(struct proc *p, uint32_t state) WRITES(p->state);
struct proc *pid2proc(pid_t pid);
bool proc_controls(struct proc *SAFE actor, struct proc *SAFE target);
void proc_run(struct proc *SAFE p);
void proc_startcore(struct proc *SAFE p, trapframe_t *SAFE tf)
     __attribute__((noreturn));
void proc_destroy(struct proc *SAFE p);
void proc_yield(struct proc *SAFE p);

/* Process core management.  Only call these if you are RUNNING_M or RUNNABLE_M.
 * These all adjust the vcoremap and take appropriate actions (like __startcore
 * if you were already RUNNING_M.  You could be RUNNABLE_M with no vcores when
 * these are done (basically preempted, and waiting to get run again).
 *
 * These are internal functions.  Error checking is to catch bugs, and you
 * shouldn't call these functions with parameters you are not sure about (like
 * an invalid corelist).  
 *
 * They also may cause an IPI to be sent to core it is called on.  If so, the
 * return value will be true.  Once you unlock (and enable interrupts) you will
 * be preempted, and usually lose your stack.  There is a helper to unlock and
 * handle the refcnt.
 *
 * WARNING: YOU MUST HOLD THE PROC_LOCK BEFORE CALLING THESE! */
/* Gives process p the additional num cores listed in corelist */
bool __proc_give_cores(struct proc *SAFE p, int32_t *corelist, size_t num);
/* Makes process p's coremap look like corelist (add, remove, etc). Not used */
bool __proc_set_allcores(struct proc *SAFE p, int32_t *corelist,
                         size_t *num, amr_t message, TV(a0t) arg0,
                         TV(a1t) arg1, TV(a2t) arg2);
/* Takes from process p the num cores listed in corelist */
bool __proc_take_cores(struct proc *SAFE p, int32_t *corelist,
                       size_t num, amr_t message, TV(a0t) arg0,
                       TV(a1t) arg1, TV(a2t) arg2);
bool __proc_take_allcores(struct proc *SAFE p, amr_t message, TV(a0t) arg0,
                          TV(a1t) arg1, TV(a2t) arg2);
void __proc_unlock_ipi_pending(struct proc *p, bool ipi_pending);

/* Will probably have generic versions of these later. */
void proc_incref(struct proc *SAFE p, size_t count);
void proc_decref(struct proc *SAFE p, size_t count);
 
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
void proc_init_trapframe(trapframe_t *SAFE tf, uint32_t vcoreid,
                         uint32_t entryp, uint32_t stack_top);
void proc_set_syscall_retval(trapframe_t *SAFE tf, intreg_t value);

/* Degubbing */
void print_idlecoremap(void);
void print_allpids(void);
void print_proc_info(pid_t pid);

#endif // !ROS_KERN_PROCESS_H
