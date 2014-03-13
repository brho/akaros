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
#include <schedule.h>

/* Process States.  Not 100% on the names yet.  RUNNABLE_* are waiting to go to
 * RUNNING_*.  For instance, RUNNABLE_M is expecting to go to RUNNING_M.  It
 * could be waiting for it's timeslice, or possibly for all the cores it asked
 * for.
 *
 * Difference between the _M and the _S states:
 * - _S : legacy process mode
 * - RUNNING_M implies *guaranteed* core(s).  You can be a single core in the
 *   RUNNING_M state.  The guarantee is subject to time slicing, but when you
 *   run, you get all of your cores.
 * - The time slicing is at a coarser granularity for _M states.  This means
 *   that when you run an _S on a core, it should be interrupted/time sliced
 *   more often, which also means the core should be classified differently for
 *   a while.  Possibly even using its local APIC timer.
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

/* Can use a htable iterator to iterate through all active procs */
extern struct hashtable *pid_hash;
extern spinlock_t pid_hash_lock;

/* Initialization */
void proc_init(void);

/* Process management: */
struct proc *pid_nth(unsigned int n);
error_t proc_alloc(struct proc **pp, struct proc *parent);
void __proc_ready(struct proc *p);
struct proc *proc_create(struct file *prog, char **argv, char **envp);
int __proc_set_state(struct proc *p, uint32_t state) WRITES(p->state);
struct proc *pid2proc(pid_t pid);
bool proc_controls(struct proc *SAFE actor, struct proc *SAFE target);
void proc_incref(struct proc *p, unsigned int val);
void proc_decref(struct proc *p);
void proc_run_s(struct proc *p);
void __proc_run_m(struct proc *p);
void __proc_startcore(struct proc *p, struct user_context *ctx);
void proc_restartcore(void);
void proc_destroy(struct proc *p);
void proc_signal_parent(struct proc *child);
int __proc_disown_child(struct proc *parent, struct proc *child);
int proc_change_to_m(struct proc *p);
void __proc_save_fpu_s(struct proc *p);
void __proc_save_context_s(struct proc *p, struct user_context *ctx);
void proc_yield(struct proc *SAFE p, bool being_nice);
void proc_notify(struct proc *p, uint32_t vcoreid);
void proc_wakeup(struct proc *p);
bool __proc_is_mcp(struct proc *p);
int proc_change_to_vcore(struct proc *p, uint32_t new_vcoreid,
                         bool enable_my_notif);

/* Vcoremap info: */
uint32_t proc_get_vcoreid(struct proc *p);
/* TODO: make all of these inline once we gut the Env crap */
bool vcore_is_mapped(struct proc *p, uint32_t vcoreid);
uint32_t vcore2vcoreid(struct proc *p, struct vcore *vc);
struct vcore *vcoreid2vcore(struct proc *p, uint32_t vcoreid);

/* Process core management.  Only call these if you are RUNNING_M or RUNNABLE_M.
 * These all adjust the vcoremap and take appropriate actions (like __startcore
 * if you were already RUNNING_M.  You could be RUNNABLE_M with no vcores when
 * these are done (basically preempted, and waiting to get run again).
 *
 * These are internal functions.  Error checking is to catch bugs, and you
 * shouldn't call these functions with parameters you are not sure about (like
 * an invalid corelist).  
 *
 * WARNING: YOU MUST HOLD THE PROC_LOCK BEFORE CALLING THESE! */
/* Gives process p the additional num cores listed in corelist */
int __proc_give_cores(struct proc *p, uint32_t *pc_arr, uint32_t num);
/* Takes from process p the num cores listed in pc_arr */
void __proc_take_corelist(struct proc *p, uint32_t *pc_arr, uint32_t num,
                          bool preempt);
/* Takes all cores, returns the count, fills in pc_arr with their pcoreid */
uint32_t __proc_take_allcores(struct proc *p, uint32_t *pc_arr, bool preempt);

/* Exposed for kern/src/resource.c for now */
void __map_vcore(struct proc *p, uint32_t vcoreid, uint32_t pcoreid);
void __unmap_vcore(struct proc *p, uint32_t vcoreid);

/* Preemption management.  Some of these will change */
void __proc_preempt_warn(struct proc *p, uint32_t vcoreid, uint64_t when);
void __proc_preempt_warnall(struct proc *p, uint64_t when);
void __proc_preempt_core(struct proc *p, uint32_t pcoreid);
uint32_t __proc_preempt_all(struct proc *p, uint32_t *pc_arr);
bool proc_preempt_core(struct proc *p, uint32_t pcoreid, uint64_t usec);
void proc_preempt_all(struct proc *p, uint64_t usec);

/* Current / cr3 / context management */
struct proc *switch_to(struct proc *new_p);
void switch_back(struct proc *new_p, struct proc *old_proc);
void abandon_core(void);
void clear_owning_proc(uint32_t coreid);
void proc_tlbshootdown(struct proc *p, uintptr_t start, uintptr_t end);

/* Kernel message handlers for process management */
void __startcore(uint32_t srcid, long a0, long a1, long a2);
void __set_curctx(uint32_t srcid, long a0, long a1, long a2);
void __notify(uint32_t srcid, long a0, long a1, long a2);
void __preempt(uint32_t srcid, long a0, long a1, long a2);
void __death(uint32_t srcid, long a0, long a1, long a2);
void __tlbshootdown(uint32_t srcid, long a0, long a1, long a2);

/* Arch Specific */
void proc_pop_ctx(struct user_context *ctx) __attribute__((noreturn));
void proc_init_ctx(struct user_context *ctx, uint32_t vcoreid, uintptr_t entryp,
                   uintptr_t stack_top, uintptr_t tls_desc);
void proc_secure_ctx(struct user_context *ctx);
void __abandon_core(void);

/* Degubbing */
void print_allpids(void);
void print_proc_info(pid_t pid);

#endif /* !ROS_KERN_PROCESS_H */
