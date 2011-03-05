#ifndef _UTHREAD_H
#define _UTHREAD_H

#include <vcore.h>

/* Bare necessities of a user thread.  2LSs should allocate a bigger struct and
 * cast their threads to uthreads when talking with vcore code.  Vcore/default
 * 2LS code won't touch udata or beyond. */
struct uthread {
	struct user_trapframe utf;
	struct ancillary_state as;
	void *tls_desc;
	/* whether or not the scheduler can migrate you from your vcore */
	bool dont_migrate;
};
extern __thread struct uthread *current_uthread;

/* 2L-Scheduler operations.  Can be 0.  Examples in pthread.c. */
struct schedule_ops {
	/* Functions supporting thread ops */
	struct uthread *(*sched_init)(void);
	void (*sched_entry)(void);
	struct uthread *(*thread_create)(void (*func)(void), void *);
	void (*thread_runnable)(struct uthread *);
	void (*thread_yield)(struct uthread *);
	void (*thread_exit)(struct uthread *);
	unsigned int (*vcores_wanted)(void);
	/* Functions event handling wants */
	void (*preempt_pending)(void);
	void (*spawn_thread)(uintptr_t pc_start, void *data);	/* don't run yet */
};
extern struct schedule_ops *sched_ops;

/* Functions to make/manage uthreads.  Can be called by functions such as
 * pthread_create(), which can wrap these with their own stuff (like attrs,
 * retvals, etc). */

/* Creates a uthread.  Will pass udata to sched_ops's thread_create.  Func is
 * what gets run, and if you want args, wrap it (like pthread) */
struct uthread *uthread_create(void (*func)(void), void *udata);
void uthread_runnable(struct uthread *uthread);
void uthread_yield(void);
void uthread_exit(void);

/* Utility function.  Event code also calls this. */
bool check_preempt_pending(uint32_t vcoreid);

/* Helpers, which sched_entry() can call */
void run_current_uthread(void);
void run_uthread(struct uthread *uthread);

#endif /* _UTHREAD_H */
