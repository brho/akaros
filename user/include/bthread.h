#ifndef _BTHREAD_H
#define _BTHREAD_H

#include <vcore.h>
#include <mcs.h>

#ifdef __cplusplus
  extern "C" {
#endif

/* Detach state.  */
enum
{
  BTHREAD_CREATE_JOINABLE,
#define BTHREAD_CREATE_JOINABLE	BTHREAD_CREATE_JOINABLE
  BTHREAD_CREATE_DETACHED
#define BTHREAD_CREATE_DETACHED	BTHREAD_CREATE_DETACHED
};

struct bthread_wqt
{
  void* (*start_routine)(void*);
  void* arg;
  int finished;
  int detached;
  struct bthread_wqt* next;
};

typedef struct
{
  int type;
} bthread_mutexattr_t;

typedef struct
{
  const bthread_mutexattr_t* attr;
  int lock;
} bthread_mutex_t;

typedef struct
{
  int local_sense[32*MAX_VCORES];
  volatile int sense;
  int count;
  int nprocs;
  mcs_lock_t lock;
} bthread_barrier_t;

typedef struct
{
  int pshared;
} bthread_condattr_t;

typedef struct
{
  const bthread_condattr_t* attr;
  int waiters[MAX_VCORES];
} bthread_cond_t;

typedef struct bthread_wqt work_queue_t;
typedef work_queue_t* bthread_t;
typedef int bthread_attr_t;
typedef int bthread_barrierattr_t;
typedef int bthread_once_t;
typedef void** bthread_key_t;

#define BTHREAD_ONCE_INIT 0
#define BTHREAD_BARRIER_SERIAL_THREAD 12345
#define BTHREAD_MUTEX_INITIALIZER {0}
#define BTHREAD_MUTEX_NORMAL 0
#define BTHREAD_MUTEX_DEFAULT BTHREAD_MUTEX_NORMAL
#define BTHREAD_COND_INITIALIZER {0}
#define BTHREAD_PROCESS_PRIVATE 0

int bthread_attr_init(bthread_attr_t *);
int bthread_attr_destroy(bthread_attr_t *);
int bthread_create(bthread_t *, const bthread_attr_t *,
                   void *(*)(void *), void *);
int bthread_join(bthread_t, void **);

int bthread_attr_setdetachstate(bthread_attr_t *__attr,int __detachstate);

int bthread_mutex_destroy(bthread_mutex_t *);
int bthread_mutex_init(bthread_mutex_t *, const bthread_mutexattr_t *);
int bthread_mutex_lock(bthread_mutex_t *);
int bthread_mutex_trylock(bthread_mutex_t *);
int bthread_mutex_unlock(bthread_mutex_t *);
int bthread_mutex_destroy(bthread_mutex_t *);

int bthread_mutexattr_init(bthread_mutexattr_t *);
int bthread_mutexattr_destroy(bthread_mutexattr_t *);
int bthread_mutexattr_gettype(const bthread_mutexattr_t *, int *);
int bthread_mutexattr_settype(bthread_mutexattr_t *, int);

int bthread_cond_init(bthread_cond_t *, const bthread_condattr_t *);
int bthread_cond_destroy(bthread_cond_t *);
int bthread_cond_broadcast(bthread_cond_t *);
int bthread_cond_signal(bthread_cond_t *);
int bthread_cond_wait(bthread_cond_t *, bthread_mutex_t *);

int bthread_condattr_init(bthread_condattr_t *);
int bthread_condattr_destroy(bthread_condattr_t *);
int bthread_condattr_setpshared(bthread_condattr_t *, int);
int bthread_condattr_getpshared(bthread_condattr_t *, int *);

#define bthread_rwlock_t bthread_mutex_t
#define bthread_rwlockattr_t bthread_mutexattr_t
#define bthread_rwlock_destroy bthread_mutex_destroy
#define bthread_rwlock_init bthread_mutex_init
#define bthread_rwlock_unlock bthread_mutex_unlock
#define bthread_rwlock_rdlock bthread_mutex_lock
#define bthread_rwlock_wrlock bthread_mutex_lock
#define bthread_rwlock_tryrdlock bthread_mutex_trylock
#define bthread_rwlock_trywrlock bthread_mutex_trylock

bthread_t bthread_self();
int bthread_equal(bthread_t t1, bthread_t t2);
void bthread_exit(void* ret);
int bthread_once(bthread_once_t* once_control, void (*init_routine)(void));

int bthread_barrier_init(bthread_barrier_t* b, const bthread_barrierattr_t* a, int count);
int bthread_barrier_wait(bthread_barrier_t* b);
int bthread_barrier_destroy(bthread_barrier_t* b);

#ifdef __cplusplus
  }
#endif

#endif
