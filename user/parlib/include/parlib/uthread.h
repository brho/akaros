#pragma once

#include <parlib/vcore.h>
#include <parlib/signal.h>
#include <ros/syscall.h>

__BEGIN_DECLS

#define UTHREAD_DONT_MIGRATE		0x001 /* don't move to another vcore */
#define UTHREAD_SAVED				0x002 /* uthread's state is in utf */
#define UTHREAD_FPSAVED				0x004 /* uthread's FP state is in uth->as */
#define UTHREAD_IS_THREAD0			0x008 /* thread0: glibc's main() thread */

/* Thread States */
#define UT_RUNNING		1
#define UT_NOT_RUNNING	2

/* Externally blocked thread reasons (for uthread_has_blocked()) */
#define UTH_EXT_BLK_MUTEX			1
#define UTH_EXT_BLK_EVENTQ			2
#define UTH_EXT_BLK_JUSTICE			3	/* whatever.  might need more options */

/* Bare necessities of a user thread.  2LSs should allocate a bigger struct and
 * cast their threads to uthreads when talking with vcore code.  Vcore/default
 * 2LS code won't touch udata or beyond. */
struct uthread {
	struct user_context u_ctx;
	struct ancillary_state as;
	void *tls_desc;
	int flags;
	int state;
	struct sigstate sigstate;
	int notif_disabled_depth;
	struct syscall *sysc;	/* syscall we're blocking on, if any */
	struct syscall local_sysc;	/* for when we don't want to use the stack */
	void (*yield_func)(struct uthread*, void*);
	void *yield_arg;
	int err_no;
	char err_str[MAX_ERRSTR_LEN];
};
extern __thread struct uthread *current_uthread;

/* These structs are undefined.  We use them instead of void * so we can get
 * compiler warnings if someone passes the wrong pointer type.  Internally, we
 * use another struct type for mtx and cvs. */
typedef struct __uth_mtx_opaque * uth_mutex_t;
typedef struct __uth_cv_opaque * uth_cond_var_t;

/* 2L-Scheduler operations.  Examples in pthread.c. */
struct schedule_ops {
	/**** These functions must be defined ****/
	/* Functions supporting thread ops */
	void (*sched_entry)(void);
	void (*thread_runnable)(struct uthread *);
	void (*thread_paused)(struct uthread *);
	void (*thread_blockon_sysc)(struct uthread *, void *);
	void (*thread_has_blocked)(struct uthread *, int);
	void (*thread_refl_fault)(struct uthread *, struct user_context *);
	/**** Defining these functions is optional. ****/
	/* 2LSs can leave the mutex/cv funcs empty for a default implementation */
	uth_mutex_t (*mutex_alloc)(void);
	void (*mutex_free)(uth_mutex_t);
	void (*mutex_lock)(uth_mutex_t);
	void (*mutex_unlock)(uth_mutex_t);
	uth_cond_var_t (*cond_var_alloc)(void);
	void (*cond_var_free)(uth_cond_var_t);
	void (*cond_var_wait)(uth_cond_var_t, uth_mutex_t);
	void (*cond_var_signal)(uth_cond_var_t);
	void (*cond_var_broadcast)(uth_cond_var_t);
	/* Functions event handling wants */
	void (*preempt_pending)(void);
};
extern struct schedule_ops *sched_ops;

/* Low-level _S code calls this for basic uthreading without a 2LS */
void uthread_lib_init(void);
/* Call this, passing it a uthread representing thread0, from your 2LS init
 * routines.  When it returns, you're in _M mode (thread0 on vcore0) */
void uthread_2ls_init(struct uthread *uthread, struct schedule_ops *ops);
/* Call this to become an mcp capable of worling with uthreads. */
void uthread_mcp_init(void);

/* Functions to make/manage uthreads.  Can be called by functions such as
 * pthread_create(), which can wrap these with their own stuff (like attrs,
 * retvals, etc). */

/* uthread_init() does the uthread initialization of a uthread that the caller
 * created.  Call this whenever you are "starting over" with a thread.  Pass in
 * attr, if you want to override any defaults. */
struct uth_thread_attr {
	bool want_tls;		/* default, no */
};
void uthread_init(struct uthread *new_thread, struct uth_thread_attr *attr);
/* Call this when you are done with a uthread, forever, but before you free it */
void uthread_cleanup(struct uthread *uthread);
void uthread_runnable(struct uthread *uthread);
void uthread_yield(bool save_state, void (*yield_func)(struct uthread*, void*),
                   void *yield_arg);
void uthread_sleep(unsigned int seconds);
void uthread_usleep(unsigned int usecs);
void __attribute__((noreturn)) uthread_sleep_forever(void);
void uthread_has_blocked(struct uthread *uthread, int flags);
void uthread_paused(struct uthread *uthread);

/* Utility functions */
bool __check_preempt_pending(uint32_t vcoreid);	/* careful: check the code */
void uth_disable_notifs(void);
void uth_enable_notifs(void);

/* Helpers, which the 2LS can call */
void __block_uthread_on_async_sysc(struct uthread *uth);
void highjack_current_uthread(struct uthread *uthread);
struct uthread *stop_current_uthread(void);
void __attribute__((noreturn)) run_current_uthread(void);
void __attribute__((noreturn)) run_uthread(struct uthread *uthread);

/* Asking for trouble with this API, when we just want stacktop (or whatever
 * the SP will be). */
static inline void init_uthread_ctx(struct uthread *uth, void (*entry)(void),
                                    void *stack_bottom, uint32_t size)
{
	init_user_ctx(&uth->u_ctx, (long)entry, (long)(stack_bottom) + size);
}

/* When we look at the current_uthread, its context might be in the uthread
 * struct or it might be in VCPD.  This returns a pointer to the right place. */
static inline struct user_context *get_cur_uth_ctx(void)
{
	if (current_uthread->flags & UTHREAD_SAVED)
		return &current_uthread->u_ctx;
	else
		return &vcpd_of(vcore_id())->uthread_ctx;
}

#define uthread_set_tls_var(uth, name, val)                                    \
({                                                                             \
	typeof(val) __val = val;                                                   \
	begin_access_tls_vars(((struct uthread*)(uth))->tls_desc);                 \
	name = __val;                                                              \
	end_access_tls_vars();                                                     \
})

#define uthread_get_tls_var(uth, name)                                         \
({                                                                             \
	typeof(name) val;                                                          \
	begin_access_tls_vars(((struct uthread*)(uth))->tls_desc);                 \
	val = name;                                                                \
	end_access_tls_vars();                                                     \
	val;                                                                       \
})

/* Generic Uthread Mutexes.  2LSs implement their own methods, but we need a
 * 2LS-independent interface and default implementation. */
uth_mutex_t uth_mutex_alloc(void);
void uth_mutex_free(uth_mutex_t m);
void uth_mutex_lock(uth_mutex_t m);
void uth_mutex_unlock(uth_mutex_t m);

/* Generic Uthread Condition Variables.  2LSs can implement their own methods.
 * Callers to cv_wait must hold the mutex, which it will atomically wait and
 * unlock, then relock when it returns.  Callers to signal and broadcast may
 * hold the mutex, if they choose. */
uth_cond_var_t uth_cond_var_alloc(void);
void uth_cond_var_free(uth_cond_var_t cv);
void uth_cond_var_wait(uth_cond_var_t cv, uth_mutex_t m);
void uth_cond_var_signal(uth_cond_var_t cv);
void uth_cond_var_broadcast(uth_cond_var_t cv);

__END_DECLS
