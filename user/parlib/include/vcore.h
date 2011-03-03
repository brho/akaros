#ifndef _VCORE_H
#define _VCORE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <arch/vcore.h>
#include <sys/param.h>
#include <string.h>

/*****************************************************************************/
/* TODO: This is a complete hack, but necessary for vcore stuff to work for now
 * The issue is that exit sometimes calls sys_yield(), and we can't recover from
 * that properly under our vcore model (we shouldn't though).  We really need to
 * rethink what sys_yield 'should' do when in multicore mode, or else come up 
 * with a different syscall entirely. */
#include <stdlib.h>
#include <unistd.h>
#undef exit
#define exit(status) ros_syscall(SYS_proc_destroy, getpid(), status, 0, 0, 0, 0)
/*****************************************************************************/

#define LOG2_MAX_VCORES 6
#define MAX_VCORES (1 << LOG2_MAX_VCORES)

#define TRANSITION_STACK_PAGES 2
#define TRANSITION_STACK_SIZE (TRANSITION_STACK_PAGES*PGSIZE)

/* Defined by glibc; Must be implemented by a user level threading library */
extern void vcore_entry();
/* Declared in glibc's start.c */
extern __thread bool __vcore_context;

/* Utility Functions */
void *allocate_tls(void);

/* Vcore API functions */
static inline size_t max_vcores(void);
static inline size_t num_vcores(void);
static inline int vcore_id(void);
static inline bool in_vcore_context(void);
static inline void enable_notifs(uint32_t vcoreid);
static inline void disable_notifs(uint32_t vcoreid);
int vcore_init(void);
int vcore_request(size_t k);
void vcore_yield(void);
bool check_preempt_pending(uint32_t vcoreid);
void clear_notif_pending(uint32_t vcoreid);

/* Bare necessities of a user thread.  2LSs should allocate a bigger struct and
 * cast their threads to uthreads when talking with vcore code.  Vcore/default
 * 2LS code won't touch udata or beyond. */
struct uthread {
	struct user_trapframe utf;
	struct ancillary_state as;
	void *tls_desc;
	/* these four could be put in the 2LSs, but since their main use is by
	 * init_user_tf, which mucks with utf, we'll keep it in vcore code for now*/
	void *(*start_routine)(void*);
	void *arg;
	void *stacktop;
	void *retval;	/* tied to join, which isn't as clean as i'd like */
	/* whether or not the scheduler can migrate you from your vcore */
	bool dont_migrate;
};
extern __thread struct uthread *current_thread;

/* 2L-Scheduler operations.  Can be 0.  Examples in pthread.c. */
struct schedule_ops {
	/* Functions supporting thread ops */
	struct uthread *(*sched_init)(void);
	void (*sched_entry)(void);
	struct uthread *(*thread_create)(void *);
	void (*thread_runnable)(struct uthread *);
	void (*thread_yield)(struct uthread *);
	void (*thread_exit)(struct uthread *);
	/* Functions event handling wants */
	void (*preempt_pending)(void);
	void (*spawn_thread)(uintptr_t pc_start, void *data);	/* don't run yet */
};
extern struct schedule_ops *sched_ops;

/* Functions to make/manage uthreads.  Can be called by functions such as
 * pthread_create(), which can wrap these with their own stuff (like attrs,
 * retvals, etc). */

/* Creates a uthread.  Will pass udata to sched_ops's thread_create.  For now,
 * the vcore/default 2ls code handles start routines and args.  Mostly because
 * this is used when initing a utf, which is vcore specific for now. */
struct uthread *uthread_create(void *(*start_routine)(void *), void *arg,
                               void *udata);
void uthread_yield(void);
void uthread_exit(void *retval);

/* Helpers, which sched_entry() can call */
void run_current_uthread(void);
void run_uthread(struct uthread *uthread);

/* Static inlines */
static inline size_t max_vcores(void)
{
	return MIN(__procinfo.max_vcores, MAX_VCORES);
}

static inline size_t num_vcores(void)
{
	return __procinfo.num_vcores;
}

static inline int vcore_id(void)
{
	return __vcoreid;
}

static inline bool in_vcore_context(void)
{
	return __vcore_context;
}

static inline void enable_notifs(uint32_t vcoreid)
{
	__procdata.vcore_preempt_data[vcoreid].notif_enabled = TRUE;
}

static inline void disable_notifs(uint32_t vcoreid)
{
	__procdata.vcore_preempt_data[vcoreid].notif_enabled = FALSE;
}

#ifdef __cplusplus
}
#endif

#endif
