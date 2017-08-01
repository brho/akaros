/* Copyright (c) 2016 Google Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * 2LS for virtual machines */

#pragma once

#include <parlib/uthread.h>
#include <sys/queue.h>

__BEGIN_DECLS

/* Three types of threads.  Guests are actual guest VMs.  Controllers are
 * threads that are paired to guests and handles their exits.  Guests and
 * controllers are 1:1 (via *buddy).  Task threads are for the VMM itself, such
 * as a console thread. */

#define VMM_THREAD_GUEST		1
#define VMM_THREAD_CTLR			2
#define VMM_THREAD_TASK			3

#define VMM_THR_STACKSIZE		(4 * PGSIZE)

struct guest_thread;
struct ctlr_thread;
struct task_thread;

struct guest_thread {
	struct uthread				uthread;
	struct ctlr_thread			*buddy;
	unsigned int				gpc_id;
	bool						halt_exit;
	uth_mutex_t					*halt_mtx;
	uth_cond_var_t				*halt_cv;
	unsigned long				nr_vmexits;
	// TODO: work out a real ops strategy.
	bool (*vmcall)(struct guest_thread *gth, struct vm_trapframe *);
};

struct ctlr_thread {
	struct uthread				uthread;
	struct guest_thread			*buddy;
	size_t						stacksize;
	void						*stacktop;
};

struct task_thread {
	struct uthread				uthread;
	void						*(*func)(void *);
	void						*arg;
	size_t						stacksize;
	void						*stacktop;
};

struct virtual_machine;			/* in vmm/vmm.h */
struct vmm_thread {
	union {
		struct guest_thread		guest;
		struct ctlr_thread		ctlr;
		struct task_thread		task;
	};
	int							type;
	TAILQ_ENTRY(vmm_thread)		tq_next;
	struct virtual_machine		*vm;
	/* Sched stats */
	int							prev_vcoreid;
	unsigned long				nr_runs;
	unsigned long				nr_resched;
};

TAILQ_HEAD(vmm_thread_tq, vmm_thread);

extern int vmm_sched_period_usec;

/* Initialize a VMM for a virtual machine, which the caller fills out, except
 * for gths.  This will set **gths in the struct virtual machine.  Do not free()
 * the array.
 *
 * Set the parlib control variables (e.g. parlib_wants_to_be_mcp) before calling
 * this initializer.
 *
 * Returns 0 on success, -1 o/w. */
int vmm_init(struct virtual_machine *vm, int flags);
/* Starts a guest thread/core. */
void start_guest_thread(struct guest_thread *gth);
/* Start and run a task thread. */
struct task_thread *vmm_run_task(struct virtual_machine *vm,
                                 void *(*func)(void *), void *arg);

int vthread_attr_init(struct virtual_machine *vm, int vmmflags);
int vthread_attr_kernel_init(struct virtual_machine *vm, int vmmflags);
int vthread_create(struct virtual_machine *vm, int guest, void *rip, void *arg);

__END_DECLS
