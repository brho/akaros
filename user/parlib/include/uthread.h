#ifndef _UTHREAD_H
#define _UTHREAD_H

#include <vcore.h>
#include <ros/syscall.h>

#define UTHREAD_DONT_MIGRATE		0x001 /* don't move to another vcore */
#define UTHREAD_SAVED				0x002 /* uthread's state is in utf */
#define UTHREAD_FPSAVED				0x004 /* uthread's FP state is in uth->as */

/* Thread States */
#define UT_CREATED	1
#define UT_RUNNABLE	2
#define UT_RUNNING	3
#define UT_BLOCKED	4
#define UT_DYING	5

/* Bare necessities of a user thread.  2LSs should allocate a bigger struct and
 * cast their threads to uthreads when talking with vcore code.  Vcore/default
 * 2LS code won't touch udata or beyond. */
struct uthread {
	struct user_trapframe utf;
	struct ancillary_state as;
	void *tls_desc;
	int flags;
	struct syscall *sysc;	/* syscall we're blocking on, if any */
	int state;
};
extern __thread struct uthread *current_uthread;

/* 2L-Scheduler operations.  Can be 0.  Examples in pthread.c. */
struct schedule_ops {
	/* Functions supporting thread ops */
	void (*sched_entry)(void);
	void (*thread_runnable)(struct uthread *);
	void (*thread_yield)(struct uthread *);
	void (*thread_paused)(struct uthread *);
	void (*thread_blockon_sysc)(struct syscall *);
	/* Functions event handling wants */
	void (*preempt_pending)(void);
	void (*spawn_thread)(uintptr_t pc_start, void *data);	/* don't run yet */
};
extern struct schedule_ops *sched_ops;

/* Call this, passing it a uthread representing thread0, from your 2LS init
 * routines.  When it returns, you're in _M mode (thread0 on vcore0) */
int uthread_lib_init(struct uthread *uthread);

/* Functions to make/manage uthreads.  Can be called by functions such as
 * pthread_create(), which can wrap these with their own stuff (like attrs,
 * retvals, etc). */

/* Does the uthread initialization of a uthread that the caller created.  Call
 * this whenever you are "starting over" with a thread. */
void uthread_init(struct uthread *new_thread);
/* Low-level _S code calls this for basic uthreading without a 2LS */
void uthread_slim_init(void);
/* Call this when you are done with a uthread, forever, but before you free it */
void uthread_cleanup(struct uthread *uthread);
void uthread_runnable(struct uthread *uthread);
void uthread_yield(bool save_state);

/* Utility functions */
bool __check_preempt_pending(uint32_t vcoreid);	/* careful: check the code */
void uth_disable_notifs(void);
void uth_enable_notifs(void);
void copyout_uthread(struct preempt_data *vcpd, struct uthread *uthread);

bool register_evq(struct syscall *sysc, struct event_queue *ev_q);
void deregister_evq(struct syscall *sysc);

/* Helpers, which sched_entry() can call */
void run_current_uthread(void);
void run_uthread(struct uthread *uthread);

#endif /* _UTHREAD_H */
