
#ifndef THREADLIB_H
#define THREADLIB_H

#include "pthread.h"
#include "resource_stats.h"
#include <stdbool.h>
#include <time.h>
#include <signal.h>

#define likely(x)   __builtin_expect(!!(x),1)
#define unlikely(x) __builtin_expect(!!(x),0)

struct thread_st;
typedef struct thread_st thread_t;

typedef enum {
  UNLOCKED = 0,
  LOCKED   = 1,
} lock_state_t;

typedef struct latch_st {
  thread_t *state;
} latch_t;

// Queue used for mutex implementation
typedef struct queue_st {
  void **data;   /* allocated data */
  int size;  /* allocated size */
  int head, tail;   /* valid data in [head, tail) */
} queue_t;

typedef struct {
  latch_t latch;
  lock_state_t state;
  int count;   // for recursive locking
  char *name;
  queue_t wait_queue;
  thread_t *owner;   // for sanity checking in thread_mutex_unlock()
} mutex_t;

// the read-write lock structure
typedef struct rwlock_st { /* not hidden to avoid destructor */
  int            rw_state;
  unsigned int   rw_mode;
  unsigned long  rw_readers;
  mutex_t    rw_mutex_rd;
  mutex_t    rw_mutex_rw;
} rwlock_t;

// the condition variable structure
typedef struct cond_st { /* not hidden to avoid destructor */
  unsigned long cn_state;
  unsigned int  cn_waiters;
  queue_t wait_queue;
} cond_t;


// All thread attributes
enum {
  THREAD_ATTR_JOINABLE = 1,
  THREAD_ATTR_NAME,  // Not implemented
  THREAD_ATTR_PRIO   // Not implemented
};

// Thread joinable attribute
enum {
  THREAD_CREATE_DETACHED = 0,
  THREAD_CREATE_JOINABLE = 1
};

enum {
	OK = 0,
	TIMEDOUT = 1,
	INTERRUPTED = 2,
};

typedef struct _thread_attr *thread_attr_t;

extern void switch_to_vcore();
extern void run_next_thread();
void thread_exit(void *ret);
void thread_exit_program(int exitcode);
void thread_yield();
thread_t *thread_spawn(char *name, void* (*func)(void *), void *arg);
thread_t *thread_spawn_with_attr(char *name, void* (*func)(void *), 
			    void *arg, thread_attr_t attr);  // added for pthread layer
// suspend the thread, optionally for a limited period of time (timeout)
// timeout == 0 -> suspend infinitely unless resumed by another thread
// return value: OK if resumed by someone else, TIMEDOUT is timedout
//               INTERRUPTED is interrupted by a signal
int thread_suspend_self(unsigned long long timeout);

// resume is made idempotent - resuming an already runnable thread does nothing
void thread_resume(thread_t* t);
char* thread_name(thread_t *t);
int thread_join(thread_t *t, void **val);
void thread_set_daemon(thread_t *t);

extern __thread thread_t *current_thread;
static inline thread_t* thread_self() { return current_thread; }

// Key-based thread specific storage
typedef int thread_key_t;
#define THREAD_DESTRUCTOR_ITERATIONS 4
#define THREAD_KEY_MAX 256  // NOTE: change the size of thread_t.data_count if you change this!
int thread_key_create(thread_key_t *key, void (*destructor)(void *));
int thread_key_delete(thread_key_t key);
int thread_key_setdata(thread_key_t key, const void *value);
void *thread_key_getdata(thread_key_t key);

void thread_usleep(unsigned long long timeout);

// Mutex - return TRUE on success
int thread_mutex_init(mutex_t *m, char *name);
int thread_mutex_lock(mutex_t *m);
int thread_mutex_trylock(mutex_t *m);    // do not block, return FALSE when mutex held but others
int thread_mutex_unlock(mutex_t *m);

// Rwlocks
enum rwlock_op {
  RWLOCK_RD = 1,
  RWLOCK_RW
};

int thread_rwlock_init(rwlock_t *l);
int thread_rwlock_lock(rwlock_t *l, int op);
int thread_rwlock_trylock(rwlock_t *l, int op);
int thread_rwlock_unlock(rwlock_t *l);

// Condition variables
int thread_cond_init(cond_t *c);
int thread_cond_wait(cond_t *c, mutex_t *m);
int thread_cond_timedwait(cond_t *c, mutex_t *m, const struct timespec *timeout);
int thread_cond_signal(cond_t *c);
int thread_cond_broadcast(cond_t *c);

// attribute
thread_attr_t thread_attr_of(thread_t *t);
thread_attr_t thread_attr_new();
int thread_attr_init(thread_attr_t attr);
int thread_attr_set(thread_attr_t attr, int field, ...);
int thread_attr_get(thread_attr_t attr, int field, ...);
int thread_attr_destroy(thread_attr_t attr);

unsigned thread_tid(thread_t *t);

int thread_kill(thread_t* t, int sig);
int thread_kill_all(int sig);
int thread_sigwait(const sigset_t *set, int *sig);

extern void thread_stats_add_heap(long size);
extern void thread_stats_add_fds(int num);


typedef struct {
  long active;
  long long requests;
  long long completions;
  long long bytes_read;
  long long bytes_written;
  long long errors;
} iostats_t;

extern iostats_t sockio_stats;
extern iostats_t diskio_stats;

#define IOSTAT_START(type) {\
  type##_stats.active++; \
  type##_stats.requests++; \
}
#define IOSTAT_DONE(type, success) {\
  type##_stats.active--; \
  if( (success) ) type##_stats.completions++; \
  else            type##_stats.errors++; \
}

extern const char *cap_current_syscall;  // used to inform the BG routines how they got there...
#define CAP_SET_SYSCALL()   if(!cap_current_syscall) cap_current_syscall = __FUNCTION__
#define CAP_CLEAR_SYSCALL() (cap_current_syscall = NULL)

extern void set_io_polling_func( void (*func)(long long) );

// latches

// FIXME: if there is a single kernel thread, it is an error if the latch is already locked 
// FIXME: need a spinlock, test&set, futex, etc. for multiple kernel threads 
#define LATCH_UNLOCKED NULL
#define LATCH_UNKNOWN ((thread_t*)-1)
#if OPTIMIZE < 2
#define thread_latch(latch) \
do {\
  assert(latch.state == LATCH_UNLOCKED); \
  latch.state = thread_self() ? thread_self() : LATCH_UNKNOWN; \
} while(0)
#else
#define thread_latch(latch) do { } while(0)
#endif
 
#if OPTIMIZE < 2
#define thread_unlatch(latch) \
do { \
  assert(latch.state != LATCH_UNLOCKED); \
  latch.state = UNLOCKED; \
} while(0)
#else
#define thread_unlatch(latch) do { (void)(latch);} while(0)
#endif

#if OPTIMIZE < 2
#define thread_latch_init(latch) \
do { \
  latch.state = LATCH_UNLOCKED; \
} while(0)
#else
#define thread_latch_init(latch) do {(void)(latch);} while(0)
#endif

#define LATCH_INITIALIZER_UNLOCKED { LATCH_UNLOCKED }
#define LATCH_INITIALIZER_LOCKED   { LATCH_UNKNOWN }

#endif /* THREADLIB_H */



