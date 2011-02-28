/* Copyright (c) 2009, 2010 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * All things processes!  As we move away from the old envs to processes,
 * we'll move things into here that are designed for multicore processes. */

#ifndef ROS_KERN_PROCESS_H
#define ROS_KERN_PROCESS_H

#include <ros/common.h>
#include <ros/event.h>
#include <trap.h>
#include <atomic.h>
#include <kref.h>

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

#define procstate2str(state) ((state)==PROC_CREATED    ? "CREATED"    : \
                              (state)==PROC_RUNNABLE_S ? "RUNNABLE_S" : \
                              (state)==PROC_RUNNING_S  ? "RUNNING_S"  : \
                              (state)==PROC_WAITING    ? "WAITING"    : \
                              (state)==PROC_DYING      ? "DYING"      : \
                              (state)==PROC_RUNNABLE_M ? "RUNNABLE_M" : \
                              (state)==PROC_RUNNING_M  ? "RUNNING_M"  : \
                                                         "UNKNOWN")

#include <env.h>

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
error_t proc_alloc(struct proc **pp, struct proc *parent);
void __proc_ready(struct proc *p);
struct proc *proc_create(struct file *prog, char **argv, char **envp);
int __proc_set_state(struct proc *p, uint32_t state) WRITES(p->state);
struct proc *pid2proc(pid_t pid);
bool proc_controls(struct proc *SAFE actor, struct proc *SAFE target);
void proc_incref(struct proc *p, unsigned int val);
void proc_decref(struct proc *p);
void proc_run(struct proc *SAFE p);
void proc_restartcore(void);
void proc_destroy(struct proc *SAFE p);
void __proc_yield_s(struct proc *p, struct trapframe *tf);
void proc_yield(struct proc *SAFE p, bool being_nice);
void proc_notify(struct proc *p, uint32_t vcoreid);

/* Exposed for sys_getvcoreid(), til it's unnecessary */
uint32_t proc_get_vcoreid(struct proc *SAFE p, uint32_t pcoreid);

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
bool __proc_give_cores(struct proc *SAFE p, uint32_t *pcorelist, size_t num);
/* Makes process p's coremap look like corelist (add, remove, etc). Not used */
bool __proc_set_allcores(struct proc *SAFE p, uint32_t *pcorelist,
                         size_t *num, amr_t message, TV(a0t) arg0,
                         TV(a1t) arg1, TV(a2t) arg2);
/* Takes from process p the num cores listed in corelist */
bool __proc_take_cores(struct proc *SAFE p, uint32_t *pcorelist,
                       size_t num, amr_t message, TV(a0t) arg0,
                       TV(a1t) arg1, TV(a2t) arg2);
bool __proc_take_allcores(struct proc *SAFE p, amr_t message, TV(a0t) arg0,
                          TV(a1t) arg1, TV(a2t) arg2);
void __proc_kmsg_pending(struct proc *p, bool ipi_pending);
/* Exposed for kern/src/resource.c for now */
void __map_vcore(struct proc *p, uint32_t vcoreid, uint32_t pcoreid);
void __unmap_vcore(struct proc *p, uint32_t vcoreid);

/* Preemption management.  Some of these will change */
void __proc_preempt_warn(struct proc *p, uint32_t vcoreid, uint64_t when);
void __proc_preempt_warnall(struct proc *p, uint64_t when);
bool __proc_preempt_core(struct proc *p, uint32_t pcoreid);
bool __proc_preempt_all(struct proc *p);
void proc_preempt_core(struct proc *p, uint32_t pcoreid, uint64_t usec);
void proc_preempt_all(struct proc *p, uint64_t usec);

void abandon_core(void);
/* Hold the proc_lock, since it'll use the vcoremapping to send an unmapping
 * message for the region from start to end.  */
void __proc_tlbshootdown(struct proc *p, uintptr_t start, uintptr_t end);

/* Kernel message handlers for process management */
void __startcore(trapframe_t *tf, uint32_t srcid, void *a0, void *a1, void *a2);
void __notify(trapframe_t *tf, uint32_t srcid, void *a0, void *a1, void *a2);
void __preempt(trapframe_t *tf, uint32_t srcid, void *a0, void *a1, void *a2);
void __death(trapframe_t *tf, uint32_t srcid, void *a0, void *a1, void *a2);
void __tlbshootdown(struct trapframe *tf, uint32_t srcid, void *a0, void *a1,
                    void *a2);

/* Arch Specific */
void proc_init_trapframe(trapframe_t *SAFE tf, uint32_t vcoreid,
                         uint32_t entryp, uint32_t stack_top);
void proc_secure_trapframe(struct trapframe *tf);
void __abandon_core(void);

/* Degubbing */
void print_idlecoremap(void);
void print_allpids(void);
void print_proc_info(pid_t pid);

#endif /* !ROS_KERN_PROCESS_H */
