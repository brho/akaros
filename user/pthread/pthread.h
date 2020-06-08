#pragma once

#include <sys/queue.h>
#include <signal.h>
#include <parlib/vcore.h>
#include <parlib/uthread.h>
#include <parlib/mcs.h>
#include <parlib/dtls.h>
#include <parlib/spinlock.h>
#include <parlib/signal.h>
#include <parlib/parlib.h>
/* GNU / POSIX scheduling crap */
#include <sched.h>

__BEGIN_DECLS

/* Pthread states.  These are mostly examples for other 2LSs */
#define PTH_CREATED		1
#define PTH_RUNNABLE		2
#define PTH_RUNNING		3
#define PTH_EXITING		4
#define PTH_BLK_YIELDING	5	/* btw pth_yield and pth_runnable */
#define PTH_BLK_SYSC		6	/* blocked on a syscall */
#define PTH_BLK_MUTEX		7	/* blocked externally */
#define PTH_BLK_PAUSED		8	/* handed back from uthread code */
#define PTH_BLK_MISC		9	/* catch-all from uthread code */

/* Entry for a pthread_cleanup_routine on the stack of cleanup handlers. */
struct pthread_cleanup_routine {
	SLIST_ENTRY(pthread_cleanup_routine) cr_next;
	void (*routine)(void *);
	void *arg;
};
SLIST_HEAD(pthread_cleanup_stack, pthread_cleanup_routine);

/* Pthread struct.  First has to be the uthread struct, which the vcore code
 * will access directly (as if pthread_tcb is a struct uthread). */
struct pthread_tcb;
struct pthread_tcb {
	struct uthread uthread;
	TAILQ_ENTRY(pthread_tcb) tq_next;
	int state;
	uint32_t id;
	uint64_t fork_generation;
	uint32_t stacksize;
	void *stacktop;
	void *(*start_routine)(void*);
	void *arg;
	struct pthread_cleanup_stack cr_stack;
};
typedef struct pthread_tcb* pthread_t;
#define pthread_id() (pthread_self()->id)
TAILQ_HEAD(pthread_queue, pthread_tcb);

/* Per-vcore data structures to manage syscalls.  The ev_q is where we tell the
 * kernel to signal us.  We don't need a lock since this is per-vcore and
 * accessed in vcore context. */
struct sysc_mgmt {
	struct event_queue 		*ev_q;
};

#define PTHREAD_ONCE_INIT PARLIB_ONCE_INIT
#define PTHREAD_BARRIER_SERIAL_THREAD 12345
#define PTHREAD_BARRIER_SPINS 100 // totally arbitrary
#define PTHREAD_PROCESS_PRIVATE 0
#define PTHREAD_PROCESS_SHARED 1

#define PTHREAD_MUTEX_NORMAL 0
#define PTHREAD_MUTEX_RECURSIVE 1
#define PTHREAD_MUTEX_DEFAULT PTHREAD_MUTEX_NORMAL
typedef struct {
	int type;
} pthread_mutexattr_t;

typedef struct {
	int				type;
	uth_mutex_t			mtx;
	uth_recurse_mutex_t		r_mtx;
} pthread_mutex_t;
#define PTHREAD_MUTEX_INITIALIZER { PTHREAD_MUTEX_DEFAULT, UTH_MUTEX_INIT, \
                                    UTH_RECURSE_MUTEX_INIT }

typedef int clockid_t;
typedef struct {
	int				pshared;
	clockid_t			clock;
} pthread_condattr_t;
#define PTHREAD_COND_INITIALIZER UTH_COND_VAR_INIT

/* Regarding the spinlock vs MCS, I don't expect this lock to be heavily
 * contended.  Most of the time, the caller already holds the mutex associated
 * with the cond var. */
typedef uth_cond_var_t pthread_cond_t;

typedef uth_rwlock_t pthread_rwlock_t;
#define PTHREAD_RWLOCK_INITIALIZER UTH_RWLOCK_INIT
/* Will try to avoid the rwlockattr_t business for now */
typedef void * pthread_rwlockattr_t;

typedef struct
{
	int				total_threads;
	/* state of barrier, flips btw runs */
	volatile int			sense;
	atomic_t			count;
	struct spin_pdr_lock		lock;
	uth_sync_t			waiters;
	int				nr_waiters;
} pthread_barrier_t;

/* Detach state.  */
enum
{
  PTHREAD_CREATE_JOINABLE,
#define PTHREAD_CREATE_JOINABLE	PTHREAD_CREATE_JOINABLE
  PTHREAD_CREATE_DETACHED
#define PTHREAD_CREATE_DETACHED	PTHREAD_CREATE_DETACHED
};

#define PTHREAD_STACK_PAGES 1024
#define PTHREAD_STACK_SIZE (PTHREAD_STACK_PAGES*PGSIZE)
#define PTHREAD_STACK_MIN PTHREAD_STACK_SIZE

typedef struct 
{
	void *stackaddr;
	size_t stacksize;
	size_t guardsize;
	int detachstate;
	int sched_priority;
	int sched_policy;
	int sched_inherit;
} pthread_attr_t;
typedef int pthread_barrierattr_t;
typedef parlib_once_t pthread_once_t;
typedef dtls_key_t pthread_key_t;

/* Akaros pthread extensions / hacks */
void pthread_need_tls(bool need);			/* default is TRUE */
void pthread_mcp_init(void);
void __pthread_generic_yield(struct pthread_tcb *pthread);

/* Profiling alarms for pthreads.  (profalarm.c) */
void enable_profalarm(uint64_t usecs);
void disable_profalarm(void);

/* The pthreads API */
int pthread_attr_init(pthread_attr_t *);
int pthread_attr_destroy(pthread_attr_t *);
int __pthread_create(pthread_t *, const pthread_attr_t *,
                     void *(*)(void *), void *);
int pthread_create(pthread_t *, const pthread_attr_t *,
                   void *(*)(void *), void *);
int pthread_detach(pthread_t __th);
int pthread_join(pthread_t, void **);
int pthread_yield(void);

int pthread_attr_setdetachstate(pthread_attr_t *__attr,int __detachstate);
int pthread_attr_setstacksize(pthread_attr_t *attr, size_t stacksize);
int pthread_attr_getstacksize(const pthread_attr_t *attr, size_t *stacksize);
int pthread_attr_setguardsize(pthread_attr_t *attr, size_t guardsize);
int pthread_attr_getguardsize(pthread_attr_t *attr, size_t *guardsize);

int pthread_mutex_destroy(pthread_mutex_t *);
int pthread_mutex_init(pthread_mutex_t *, const pthread_mutexattr_t *);
int pthread_mutex_lock(pthread_mutex_t *);
int pthread_mutex_trylock(pthread_mutex_t *);
int pthread_mutex_unlock(pthread_mutex_t *);
int pthread_mutex_destroy(pthread_mutex_t *);

int pthread_mutexattr_init(pthread_mutexattr_t *);
int pthread_mutexattr_destroy(pthread_mutexattr_t *);
int pthread_mutexattr_gettype(const pthread_mutexattr_t *, int *);
int pthread_mutexattr_settype(pthread_mutexattr_t *, int);

int pthread_cond_init(pthread_cond_t *, const pthread_condattr_t *);
int pthread_cond_destroy(pthread_cond_t *);
int pthread_cond_broadcast(pthread_cond_t *);
int pthread_cond_signal(pthread_cond_t *);
int pthread_cond_wait(pthread_cond_t *, pthread_mutex_t *);

int pthread_condattr_init(pthread_condattr_t *);
int pthread_condattr_destroy(pthread_condattr_t *);
int pthread_condattr_getpshared(pthread_condattr_t *, int *);
int pthread_condattr_setpshared(pthread_condattr_t *, int);
int pthread_condattr_getclock(const pthread_condattr_t *attr,
                              clockid_t *clock_id);
int pthread_condattr_setclock(pthread_condattr_t *attr, clockid_t clock_id);

int pthread_rwlock_init(pthread_rwlock_t *rwl, const pthread_rwlockattr_t *a);
int pthread_rwlock_destroy(pthread_rwlock_t *rwl);
int pthread_rwlock_rdlock(pthread_rwlock_t *rwl);
int pthread_rwlock_tryrdlock(pthread_rwlock_t *rwl);
int pthread_rwlock_wrlock(pthread_rwlock_t *rwl);
int pthread_rwlock_trywrlock(pthread_rwlock_t *rwl);
int pthread_rwlock_unlock(pthread_rwlock_t *rwl);

pthread_t pthread_self();
int pthread_equal(pthread_t t1, pthread_t t2);
void pthread_exit(void* ret);
int pthread_once(pthread_once_t* once_control, void (*init_routine)(void));

int pthread_barrier_init(pthread_barrier_t* b, const pthread_barrierattr_t* a, int count);
int pthread_barrier_wait(pthread_barrier_t* b);
int pthread_barrier_destroy(pthread_barrier_t* b);

// POSIX signal compliance
int pthread_kill (pthread_t __threadid, int __signo);
int pthread_sigmask(int how, const sigset_t *set, sigset_t *oset);
int pthread_sigqueue(pthread_t *thread, int sig, const union sigval value);

// Dynamic TLS stuff
int pthread_key_create(pthread_key_t *key, void (*destructor)(void*));
int pthread_key_delete(pthread_key_t key);
void *pthread_getspecific(pthread_key_t key);
int pthread_setspecific(pthread_key_t key, const void *value);

/* Common stuff. */
int pthread_equal(pthread_t __thread1, pthread_t __thread2);
int pthread_getattr_np(pthread_t __th, pthread_attr_t *__attr);
int pthread_attr_getstack(const pthread_attr_t *__attr,
                           void **__stackaddr, size_t *__stacksize);
int pthread_cancel(pthread_t __th);
void pthread_cleanup_push(void (*routine)(void *), void *arg);
void pthread_cleanup_pop(int execute);

/* Scheduling Stuff, mostly ignored by the actual 2LS */
int pthread_attr_setschedparam(pthread_attr_t *attr,
                               const struct sched_param *param);
int pthread_attr_getschedparam(pthread_attr_t *attr,
                               struct sched_param *param);
/* Policies are from sched.h. */
int pthread_attr_setschedpolicy(pthread_attr_t *attr, int policy);
int pthread_attr_getschedpolicy(pthread_attr_t *attr, int *policy);

#define PTHREAD_SCOPE_SYSTEM	1
#define PTHREAD_SCOPE_PROCESS	2
int pthread_attr_setscope(pthread_attr_t *attr, int scope);
int pthread_attr_getscope(pthread_attr_t *attr, int *scope);

#define PTHREAD_INHERIT_SCHED	1
#define PTHREAD_EXPLICIT_SCHED	2
int pthread_attr_setinheritsched(pthread_attr_t *attr,
                                 int inheritsched);
int pthread_attr_getinheritsched(const pthread_attr_t *attr,
                                 int *inheritsched);

int pthread_setschedparam(pthread_t thread, int policy,
                          const struct sched_param *param);
int pthread_getschedparam(pthread_t thread, int *policy,
                          struct sched_param *param);

/* Unsupported Stuff */
extern int pthread_mutex_timedlock (pthread_mutex_t *__restrict __mutex,
                    const struct timespec *__restrict
                    __abstime) __THROWNL __nonnull ((1, 2));
extern int pthread_cond_timedwait (pthread_cond_t *__restrict __cond,
                   pthread_mutex_t *__restrict __mutex,
                   const struct timespec *__restrict __abstime)
     __nonnull ((1, 2, 3));

__END_DECLS
