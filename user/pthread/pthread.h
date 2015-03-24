#ifndef _PTHREAD_H
#define _PTHREAD_H

#include <sys/queue.h>
#include <signal.h>
#include <vcore.h>
#include <uthread.h>
#include <mcs.h>
#include <dtls.h>
#include <spinlock.h>

#ifdef __cplusplus
  extern "C" {
#endif

/* Pthread states.  These are mostly examples for other 2LSs */
#define PTH_CREATED			1
#define PTH_RUNNABLE		2
#define PTH_RUNNING			3
#define PTH_EXITING			4
#define PTH_BLK_YIELDING	5	/* brief state btw pth_yield and pth_runnable */
#define PTH_BLK_JOINING		6	/* joining on a child */
#define PTH_BLK_SYSC		7	/* blocked on a syscall */
#define PTH_BLK_MUTEX		8	/* blocked externally, possibly on a mutex */
#define PTH_BLK_PAUSED		9	/* handed back to us from uthread code */

/* Pthread struct.  First has to be the uthread struct, which the vcore code
 * will access directly (as if pthread_tcb is a struct uthread). */
struct pthread_tcb;
struct pthread_tcb {
	struct uthread uthread;
	TAILQ_ENTRY(pthread_tcb) next;
	int state;
	bool detached;
	struct pthread_tcb *joiner;			/* raced on by exit and join */
	uint32_t id;
	uint32_t stacksize;
	void *stacktop;
	void *(*start_routine)(void*);
	void *arg;
	void *retval;
	sigset_t sigmask;
	sigset_t sigpending;
	struct sigdata *sigdata;
};
typedef struct pthread_tcb* pthread_t;
TAILQ_HEAD(pthread_queue, pthread_tcb);

/* Per-vcore data structures to manage syscalls.  The ev_q is where we tell the
 * kernel to signal us.  We don't need a lock since this is per-vcore and
 * accessed in vcore context. */
struct sysc_mgmt {
	struct event_queue 			*ev_q;
};

#define PTHREAD_ONCE_INIT 0
#define PTHREAD_BARRIER_SERIAL_THREAD 12345
#define PTHREAD_MUTEX_INITIALIZER {0,0}
#define PTHREAD_MUTEX_NORMAL 0
#define PTHREAD_MUTEX_DEFAULT PTHREAD_MUTEX_NORMAL
#define PTHREAD_MUTEX_SPINS 100 // totally arbitrary
#define PTHREAD_BARRIER_SPINS 100 // totally arbitrary
#define PTHREAD_COND_INITIALIZER {0,{0},{0},0}
#define PTHREAD_PROCESS_PRIVATE 0
#define PTHREAD_PROCESS_SHARED 1

typedef struct
{
  int type;
} pthread_mutexattr_t;

typedef struct
{
  const pthread_mutexattr_t* attr;
  atomic_t lock;
} pthread_mutex_t;

typedef struct
{
	int							total_threads;
	volatile int				sense;	/* state of barrier, flips btw runs */
	atomic_t					count;
	struct spin_pdr_lock		lock;
	struct pthread_queue		waiters;
	int							nr_waiters;
} pthread_barrier_t;

#define WAITER_CLEARED 0
#define WAITER_WAITING 1
#define SLOT_FREE 0
#define SLOT_IN_USE 1

/* Detach state.  */
enum
{
  PTHREAD_CREATE_JOINABLE,
#define PTHREAD_CREATE_JOINABLE	PTHREAD_CREATE_JOINABLE
  PTHREAD_CREATE_DETACHED
#define PTHREAD_CREATE_DETACHED	PTHREAD_CREATE_DETACHED
};

// TODO: how big do we want these?  ideally, we want to be able to guard and map
// more space if we go too far.
#define PTHREAD_STACK_PAGES 4
#define PTHREAD_STACK_SIZE (PTHREAD_STACK_PAGES*PGSIZE)

typedef int clockid_t;
typedef struct
{
  int pshared;
  clockid_t clock;
} pthread_condattr_t;

/* Regarding the spinlock vs MCS, I don't expect this lock to be heavily
 * contended.  Most of the time, the caller already holds the mutex associated
 * with the cond var. */
typedef struct
{
	struct pthread_queue		waiters;
	struct spin_pdr_lock 		spdr_lock;
	int 						attr_pshared;
	int 						attr_clock;
} pthread_cond_t;

typedef struct 
{
	void *stackaddr;
	size_t stacksize;
	int detachstate;
} pthread_attr_t;
typedef int pthread_barrierattr_t;
typedef int pthread_once_t;
typedef dtls_key_t pthread_key_t;

/* Akaros pthread extensions / hacks */
void pthread_can_vcore_request(bool can);	/* default is TRUE */
void pthread_need_tls(bool need);			/* default is TRUE */
void pthread_lib_init(void);
void __pthread_generic_yield(struct pthread_tcb *pthread);

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

#define pthread_rwlock_t pthread_mutex_t
#define pthread_rwlockattr_t pthread_mutexattr_t
#define pthread_rwlock_destroy pthread_mutex_destroy
#define pthread_rwlock_init pthread_mutex_init
#define pthread_rwlock_unlock pthread_mutex_unlock
#define pthread_rwlock_rdlock pthread_mutex_lock
#define pthread_rwlock_wrlock pthread_mutex_lock
#define pthread_rwlock_tryrdlock pthread_mutex_trylock
#define pthread_rwlock_trywrlock pthread_mutex_trylock

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

/* Unsupported Stuff */
typedef void *upthread_once_t;
extern int pthread_mutex_timedlock (pthread_mutex_t *__restrict __mutex,
                    const struct timespec *__restrict
                    __abstime) __THROWNL __nonnull ((1, 2));
extern int pthread_cond_timedwait (pthread_cond_t *__restrict __cond,
                   pthread_mutex_t *__restrict __mutex,
                   const struct timespec *__restrict __abstime)
     __nonnull ((1, 2, 3));
extern int pthread_once (pthread_once_t *__once_control,
             void (*__init_routine) (void)) __nonnull ((1, 2));
extern int pthread_cancel (pthread_t __th);

#ifdef __cplusplus
  }
#endif

#endif
