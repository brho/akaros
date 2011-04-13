#ifndef _PTHREAD_H
#define _PTHREAD_H

#include <sys/queue.h>
#include <vcore.h>
#include <uthread.h>
#include <mcs.h>

#ifdef __cplusplus
  extern "C" {
#endif

/* Pthread struct.  First has to be the uthread struct, which the vcore code
 * will access directly (as if pthread_tcb is a struct uthread). */
struct pthread_tcb {
	struct uthread uthread;
	TAILQ_ENTRY(pthread_tcb) next;
	int finished;
	bool detached;
	uint32_t id;
	uint32_t stacksize;
	void *(*start_routine)(void*);
	void *arg;
	void *stacktop;
	void *retval;
	uint32_t vcoreid;
};
typedef struct pthread_tcb* pthread_t;
TAILQ_HEAD(pthread_queue, pthread_tcb);

/* Per-vcore data structures to manage syscalls.  The ev_q is where we tell the
 * kernel to signal us.  The tailq is for handling overflow of syscall events.
 * The current pthread code handles syscall events (ev_qs, overflow, etc) on a
 * per-vcore basis).  We don't need a lock since this is per-vcore and accessed
 * in vcore context. */
struct sysc_mgmt {
	struct event_queue 			ev_q;
	struct pthread_queue		pending_syscs;
	bool						handling_overflow;
};

#define PTHREAD_ONCE_INIT 0
#define PTHREAD_BARRIER_SERIAL_THREAD 12345
#define PTHREAD_MUTEX_INITIALIZER {0}
#define PTHREAD_MUTEX_NORMAL 0
#define PTHREAD_MUTEX_DEFAULT PTHREAD_MUTEX_NORMAL
#define PTHREAD_MUTEX_SPINS 100 // totally arbitrary
#define PTHREAD_BARRIER_SPINS 100 // totally arbitrary
#define PTHREAD_COND_INITIALIZER {0}
#define PTHREAD_PROCESS_PRIVATE 0

typedef struct
{
  int type;
} pthread_mutexattr_t;

typedef struct
{
  const pthread_mutexattr_t* attr;
  int lock;
} pthread_mutex_t;

/* TODO: MAX_PTHREADS is arbitrarily defined for now.
 * It indicates the maximum number of threads that can wait on  
   the same cond var/ barrier concurrently. */

#define MAX_PTHREADS 32
typedef struct
{
  volatile int sense;
  int count;
  int nprocs;
  pthread_mutex_t pmutex;
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

typedef struct
{
  int pshared;
} pthread_condattr_t;


typedef struct
{
  const pthread_condattr_t* attr;
  int waiters[MAX_PTHREADS];
  int in_use[MAX_PTHREADS];
  int next_waiter; //start the search for an available waiter at this spot
} pthread_cond_t;
typedef struct 
{
	size_t stacksize;
	int detachstate;
} pthread_attr_t;
typedef int pthread_barrierattr_t;
typedef int pthread_once_t;
typedef void** pthread_key_t;

/* The pthreads API */
int pthread_attr_init(pthread_attr_t *);
int pthread_attr_destroy(pthread_attr_t *);
int pthread_create(pthread_t *, const pthread_attr_t *,
                   void *(*)(void *), void *);
int pthread_join(pthread_t, void **);
int pthread_yield(void);

int pthread_attr_setdetachstate(pthread_attr_t *__attr,int __detachstate);

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
int pthread_condattr_setpshared(pthread_condattr_t *, int);
int pthread_condattr_getpshared(pthread_condattr_t *, int *);

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

//added for redis compile
int pthread_detach(pthread_t __th);
int pthread_attr_setstacksize(pthread_attr_t *attr, size_t stacksize);
int pthread_attr_getstacksize(const pthread_attr_t *attr, size_t *stacksize);

#ifdef __cplusplus
  }
#endif

#endif
