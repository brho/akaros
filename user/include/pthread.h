#ifndef _PTHREAD_H
#define _PTHREAD_H

#include <sys/queue.h>
#include <vcore.h>
#include <mcs.h>

#ifdef __cplusplus
  extern "C" {
#endif

/* Our internal types for pthread management */
struct pthread_tcb {
	TAILQ_ENTRY(pthread_tcb) next;
	void* (*start_routine)(void*);
	void* arg;

	struct user_trapframe utf;
	struct ancillary_state as;
	void *tls_desc;
	void *stacktop;
	uint32_t id;

	int finished;
	void *retval;
	int detached;
	// whether or not the scheduler can migrate you from your vcore
	// will be slightly difficult to see this when given just a vcoreid and
	// notif_tf ptr
	bool dont_migrate;
};
typedef struct pthread_tcb* pthread_t;
TAILQ_HEAD(pthread_queue, pthread_tcb);

/* For compatibility with the existing pthread library */
enum
{
  PTHREAD_CREATE_JOINABLE,
#define PTHREAD_CREATE_JOINABLE	PTHREAD_CREATE_JOINABLE
  PTHREAD_CREATE_DETACHED
#define PTHREAD_CREATE_DETACHED	PTHREAD_CREATE_DETACHED
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

//TODO: MAX_PTHREADS is arbitrarily defined for now.
#define MAX_PTHREADS 128
typedef struct
{
  int local_sense[32*MAX_PTHREADS];
  volatile int sense;
  int count;
  int nprocs;
  pthread_mutex_t pmutex;
} pthread_barrier_t;

typedef struct
{
  int pshared;
} pthread_condattr_t;

typedef struct
{
  const pthread_condattr_t* attr;
  int waiters[MAX_PTHREADS];
} pthread_cond_t;

typedef int pthread_attr_t;
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

#ifdef __cplusplus
  }
#endif

#endif
