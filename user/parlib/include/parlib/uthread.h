#pragma once

#include <parlib/vcore.h>
#include <parlib/signal.h>
#include <parlib/spinlock.h>
#include <parlib/parlib.h>
#include <parlib/kref.h>
#include <ros/syscall.h>
#include <sys/queue.h>
#include <time.h>

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
#define UTH_EXT_BLK_YIELD			3
#define UTH_EXT_BLK_MISC			4

/* One per joiner, usually kept on the stack. */
struct uth_join_kicker {
	struct kref					kref;
	struct uthread				*joiner;
};

/* Join states, stored in the join_ctl */
#define UTH_JOIN_DETACHED		1
#define UTH_JOIN_JOINABLE		2
#define UTH_JOIN_HAS_JOINER		3
#define UTH_JOIN_EXITED			4

/* One per uthread, to encapsulate all the join fields. */
struct uth_join_ctl {
	atomic_t					state;
	void						*retval;
	void						**retval_loc;
	struct uth_join_kicker 		*kicker;
};

/* Bare necessities of a user thread.  2LSs should allocate a bigger struct and
 * cast their threads to uthreads when talking with vcore code.  Vcore/default
 * 2LS code won't touch udata or beyond. */
struct uthread {
	struct user_context u_ctx;
	struct ancillary_state as;
	void *tls_desc;
	int flags;
	int state;
	struct uth_join_ctl join_ctl;
	struct sigstate sigstate;
	int notif_disabled_depth;
	TAILQ_ENTRY(uthread) sync_next;
	struct syscall *sysc;	/* syscall we're blocking on, if any */
	struct syscall local_sysc;	/* for when we don't want to use the stack */
	void (*yield_func)(struct uthread*, void*);
	void *yield_arg;
	int err_no;
	char err_str[MAX_ERRSTR_LEN];
};
TAILQ_HEAD(uth_tailq, uthread);

extern __thread struct uthread *current_uthread;

/* This struct is a blob of sufficient storage to be whatever a 2LS wants for
 * its thread list structure (e.g., TAILQ, priority queue, RB tree, etc).
 * Internally, 2LSs and the default implementation use another object type.
 *
 * If a 2LS overrides the sync ops and uses its own synchronization, it can
 * either use the uthread->sync_next field, or add its own field to its thread
 * structure.
 *
 * If we need to increase the size, then do a full rebuild (with a make clean)
 * of the toolchain.  libgomp and probably c++ threads care about the size of
 * objects that contain uth_sync_t. */
typedef struct __uth_sync_opaque {
	uint8_t						foo[sizeof(uintptr_t) * 2];
} __attribute__ ((aligned(sizeof(uintptr_t)))) uth_sync_t;

/* 2LS-independent synchronization code (e.g. uthread mutexes) uses these
 * helpers to access 2LS-specific functions.
 *
 * Note the spinlock associated with the higher-level sync primitive is held for
 * these (where applicable). */
void __uth_sync_init(uth_sync_t *sync);
void __uth_sync_destroy(uth_sync_t *sync);
void __uth_sync_enqueue(struct uthread *uth, uth_sync_t *sync);
struct uthread *__uth_sync_get_next(uth_sync_t *sync);
bool __uth_sync_get_uth(uth_sync_t *sync, struct uthread *uth);

/* 2L-Scheduler operations.  Examples in pthread.c. */
struct schedule_ops {
	/**** These functions must be defined ****/
	void (*sched_init)(void);
	void (*sched_entry)(void);
	void (*thread_runnable)(struct uthread *);
	void (*thread_paused)(struct uthread *);
	void (*thread_blockon_sysc)(struct uthread *, void *);
	void (*thread_has_blocked)(struct uthread *, int);
	void (*thread_refl_fault)(struct uthread *, struct user_context *);
	void (*thread_exited)(struct uthread *);
	struct uthread *(*thread_create)(void *(*)(void *), void *);
	/**** Defining these functions is optional. ****/
	void (*sync_init)(uth_sync_t *);
	void (*sync_destroy)(uth_sync_t *);
	void (*sync_enqueue)(struct uthread *, uth_sync_t *);
	struct uthread *(*sync_get_next)(uth_sync_t *);
	bool (*sync_get_uth)(uth_sync_t *, struct uthread *);
	void (*preempt_pending)(void);
};
extern struct schedule_ops *sched_ops;

/* Call this from your 2LS init routines.  Pass it a uthread representing
 * thread0, your 2LS ops, and your syscall handler + data.
 *
 * When it returns, you're in _M mode (thread0 on vcore0) */
void uthread_2ls_init(struct uthread *uthread,
                      void (*handle_sysc)(struct event_msg *, unsigned int,
                                          void *),
                      void *data);
/* Call this to become an mcp capable of worling with uthreads. */
void uthread_mcp_init(void);

/* Functions to make/manage uthreads.  Can be called by functions such as
 * pthread_create(), which can wrap these with their own stuff (like attrs,
 * retvals, etc). */

struct uth_thread_attr {
	bool want_tls;		/* default, no */
	bool detached;		/* default, no */
};

struct uth_join_request {
	struct uthread				*uth;
	void						**retval_loc;
};

/* uthread_init() does the uthread initialization of a uthread that the caller
 * created.  Call this whenever you are "starting over" with a thread.  Pass in
 * attr, if you want to override any defaults. */
void uthread_init(struct uthread *new_thread, struct uth_thread_attr *attr);
/* uthread_create() is a front-end for getting the 2LS to make and run a thread
 * appropriate for running func(arg) in the GCC/glibc environment.  The thread
 * will have TLS and not be detached. */
struct uthread *uthread_create(void *(*func)(void *), void *arg);
void uthread_detach(struct uthread *uth);
void uthread_join(struct uthread *uth, void **retval_loc);
void uthread_join_arr(struct uth_join_request reqs[], size_t nr_req);
void uthread_sched_yield(void);
struct uthread *uthread_self(void);

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
void assert_can_block(void);

/* Helpers, which the 2LS can call */
void __block_uthread_on_async_sysc(struct uthread *uth);
void highjack_current_uthread(struct uthread *uthread);
struct uthread *stop_current_uthread(void);
void __attribute__((noreturn)) run_current_uthread(void);
void __attribute__((noreturn)) run_uthread(struct uthread *uthread);
void __attribute__((noreturn)) uth_2ls_thread_exit(void *retval);

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

/* Uthread Mutexes / CVs / etc. */

typedef struct uth_semaphore uth_semaphore_t;
typedef struct uth_semaphore uth_mutex_t;
typedef struct uth_recurse_mutex uth_recurse_mutex_t;
typedef struct uth_cond_var uth_cond_var_t;
typedef struct uth_rwlock uth_rwlock_t;

struct uth_semaphore {
	parlib_once_t				once_ctl;
	unsigned int				count;
	struct spin_pdr_lock		lock;
	uth_sync_t					sync_obj;
};
#define UTH_SEMAPHORE_INIT(n) { PARLIB_ONCE_INIT, (n) }
#define UTH_MUTEX_INIT { PARLIB_ONCE_INIT }

struct uth_recurse_mutex {
	parlib_once_t				once_ctl;
	uth_mutex_t					mtx;
	struct uthread				*lockholder;
	unsigned int				count;
};
#define UTH_RECURSE_MUTEX_INIT { PARLIB_ONCE_INIT }

struct uth_cond_var {
	parlib_once_t				once_ctl;
	struct spin_pdr_lock		lock;
	uth_sync_t					sync_obj;
};
#define UTH_COND_VAR_INIT { PARLIB_ONCE_INIT }

struct uth_rwlock {
	parlib_once_t				once_ctl;
	struct spin_pdr_lock		lock;
	unsigned int				nr_readers;
	bool						has_writer;
	uth_sync_t					readers;
	uth_sync_t					writers;
};
#define UTH_RWLOCK_INIT { PARLIB_ONCE_INIT }

void uth_semaphore_init(uth_semaphore_t *sem, unsigned int count);
void uth_semaphore_destroy(uth_semaphore_t *sem);
uth_semaphore_t *uth_semaphore_alloc(unsigned int count);
void uth_semaphore_free(uth_semaphore_t *sem);
bool uth_semaphore_timed_down(uth_semaphore_t *sem,
                              const struct timespec *abs_timeout);
void uth_semaphore_down(uth_semaphore_t *sem);
bool uth_semaphore_trydown(uth_semaphore_t *sem);
void uth_semaphore_up(uth_semaphore_t *sem);

void uth_mutex_init(uth_mutex_t *m);
void uth_mutex_destroy(uth_mutex_t *m);
uth_mutex_t *uth_mutex_alloc(void);
void uth_mutex_free(uth_mutex_t *m);
bool uth_mutex_timed_lock(uth_mutex_t *m, const struct timespec *abs_timeout);
void uth_mutex_lock(uth_mutex_t *m);
bool uth_mutex_trylock(uth_mutex_t *m);
void uth_mutex_unlock(uth_mutex_t *m);

void uth_recurse_mutex_init(uth_recurse_mutex_t *r_m);
void uth_recurse_mutex_destroy(uth_recurse_mutex_t *r_m);
uth_recurse_mutex_t *uth_recurse_mutex_alloc(void);
void uth_recurse_mutex_free(uth_recurse_mutex_t *r_m);
bool uth_recurse_mutex_timed_lock(uth_recurse_mutex_t *m,
                                  const struct timespec *abs_timeout);
void uth_recurse_mutex_lock(uth_recurse_mutex_t *r_m);
bool uth_recurse_mutex_trylock(uth_recurse_mutex_t *r_m);
void uth_recurse_mutex_unlock(uth_recurse_mutex_t *r_m);

/* Callers to cv_wait must hold the mutex, which it will atomically wait and
 * unlock, then relock when it returns.  Callers to signal and broadcast may
 * hold the mutex, if they choose. */
void uth_cond_var_init(uth_cond_var_t *cv);
void uth_cond_var_destroy(uth_cond_var_t *cv);
uth_cond_var_t *uth_cond_var_alloc(void);
void uth_cond_var_free(uth_cond_var_t *cv);
bool uth_cond_var_timed_wait(uth_cond_var_t *cv, uth_mutex_t *m,
                             const struct timespec *abs_timeout);
void uth_cond_var_wait(uth_cond_var_t *cv, uth_mutex_t *m);
bool uth_cond_var_timed_wait_recurse(uth_cond_var_t *cv,
                                     uth_recurse_mutex_t *r_mtx,
                                     const struct timespec *abs_timeout);
void uth_cond_var_wait_recurse(uth_cond_var_t *cv, uth_recurse_mutex_t *r_mtx);
void uth_cond_var_signal(uth_cond_var_t *cv);
void uth_cond_var_broadcast(uth_cond_var_t *cv);

void uth_rwlock_init(uth_rwlock_t *rwl);
void uth_rwlock_destroy(uth_rwlock_t *rwl);
uth_rwlock_t *uth_rwlock_alloc(void);
void uth_rwlock_free(uth_rwlock_t *rwl);
void uth_rwlock_rdlock(uth_rwlock_t *rwl);
bool uth_rwlock_try_rdlock(uth_rwlock_t *rwl);
void uth_rwlock_wrlock(uth_rwlock_t *rwl);
bool uth_rwlock_try_wrlock(uth_rwlock_t *rwl);
void uth_rwlock_unlock(uth_rwlock_t *rwl);

/* Called by gcc to see if we are multithreaded. */
bool uth_2ls_is_multithreaded(void);

__END_DECLS
