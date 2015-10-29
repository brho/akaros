/*
 * Copyright (c) 2009 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Scheduling and dispatching.
 */

#pragma once

#include <ros/common.h>
#include <sys/queue.h>

struct proc;	/* process.h includes us, but we need pointers now */
TAILQ_HEAD(proc_list, proc);		/* Declares 'struct proc_list' */

/* The ksched maintains an internal array of these: the global pcore map.  Note
 * the prov_proc and alloc_proc are weak (internal) references, and should only
 * be used as a ref source while the ksched has a valid kref. */
struct sched_pcore {
	TAILQ_ENTRY(sched_pcore)	prov_next;			/* on a proc's prov list */
	TAILQ_ENTRY(sched_pcore)	alloc_next;			/* on an alloc list (idle)*/
	struct proc					*prov_proc;			/* who this is prov to */
	struct proc					*alloc_proc;		/* who this is alloc to */
};
TAILQ_HEAD(sched_pcore_tailq, sched_pcore);

/* One of these embedded in every struct proc */
struct sched_proc_data {
	TAILQ_ENTRY(proc)			proc_link;			/* tailq linkage */
	struct proc_list 			*cur_list;			/* which tailq we're on */
	struct sched_pcore_tailq	prov_alloc_me;		/* prov cores alloced us */
	struct sched_pcore_tailq	prov_not_alloc_me;	/* maybe alloc to others */
	/* count of lists? */
	/* other accounting info */
};

void schedule_init(void);

/************** Process Management Callbacks **************/
/* Tell the ksched about the process, which it will track cradle-to-grave */
void __sched_proc_register(struct proc *p);

/* The proc was an SCP and is becoming an MCP */
void __sched_proc_change_to_m(struct proc *p);

/* The proc is dying */
void __sched_proc_destroy(struct proc *p, uint32_t *pc_arr, uint32_t nr_cores);

/* Makes sure p is runnable. */
void __sched_mcp_wakeup(struct proc *p);
void __sched_scp_wakeup(struct proc *p);

/* Gets called when a pcore becomes idle (like in proc yield).  These are 'cg'
 * cores, given to MCPs, that have been async returned to the ksched. */
void __sched_put_idle_core(struct proc *p, uint32_t coreid);
void __sched_put_idle_cores(struct proc *p, uint32_t *pc_arr, uint32_t num);

/************** Decision making **************/
/* Call the main scheduling algorithm.  Not clear yet if the main kernel will
 * ever call this directly. */
void run_scheduler(void);

/* Proc p's resource desires changed, or something in general that would lead to
 * a new decision.  The process can directly poke the ksched via a syscall, so
 * be careful of abuse. */
void poke_ksched(struct proc *p, unsigned int res_type);

/* The calling cpu/core has nothing to do and plans to idle/halt.  This is an
 * opportunity to pick the nature of that halting (low power state, etc), or
 * provide some other work (_Ss on LL cores). */
void cpu_bored(void);

/* Available resources changed (plus or minus).  Some parts of the kernel may
 * call this if a particular resource that is 'quantity-based' changes.  Things
 * like available RAM to processes, bandwidth, etc.  Cores would probably be
 * inappropriate, since we need to know which specific core is now free. */
void avail_res_changed(int res_type, long change);

/* Get and put idle CG cores.  Getting a core removes it from the idle list, and
 * the kernel can do whatever it wants with it.  All this means is that the
 * ksched won't hand out that core to a process.  This will not give out
 * provisioned cores.
 *
 * The gets return the coreid on success, -1 or -error on failure. */
int get_any_idle_core(void);
int get_specific_idle_core(int coreid);
void put_idle_core(int coreid);

/************** Proc's view of the world **************/
/* How many vcores p will think it can have */
uint32_t max_vcores(struct proc *p);

/************** Provisioning / Allocating *************/
/* This section is specific to a provisioning ksched.  Careful calling any of
 * this from generic kernel code, since it might not be present in all kernel
 * schedulers. */
int provision_core(struct proc *p, uint32_t pcoreid);

/************** Debugging **************/
void sched_diag(void);
void print_idlecoremap(void);
void print_resources(struct proc *p);
void print_all_resources(void);
void print_prov_map(void);
void next_core(uint32_t pcoreid);
void sort_idles(void);
