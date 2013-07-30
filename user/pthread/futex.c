#include <ros/common.h>
#include <futex.h>
#include <sys/queue.h>
#include <pthread.h>
#include <parlib.h>
#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include <slab.h>
#include <mcs.h>

static inline int futex_wake(int *uaddr, int count);
static inline int futex_wait(int *uaddr, int val, uint64_t ms_timeout);
static void *timer_thread(void *arg);

struct futex_element {
  TAILQ_ENTRY(futex_element) link;
  pthread_t pthread;
  int *uaddr;
  uint64_t ms_timeout;
  bool timedout;
};
TAILQ_HEAD(futex_queue, futex_element);

struct futex_data {
  struct mcs_pdr_lock lock;
  struct futex_queue queue;
  int timer_enabled;
  pthread_t timer;
  long time;
};
static struct futex_data __futex;

static inline void futex_init()
{
  mcs_pdr_init(&__futex.lock);
  TAILQ_INIT(&__futex.queue);
  __futex.timer_enabled = false;
  pthread_create(&__futex.timer, NULL, timer_thread, NULL);
  __futex.time = 0;
}

static void __futex_block(struct uthread *uthread, void *arg) {
  pthread_t pthread = (pthread_t)uthread;
  struct futex_element *e = (struct futex_element*)arg;
  __pthread_generic_yield(pthread);
  pthread->state = PTH_BLK_MUTEX;
  e->pthread = pthread;
}

static inline int futex_wait(int *uaddr, int val, uint64_t ms_timeout)
{
  // Atomically do the following...
  mcs_pdr_lock(&__futex.lock);
  // If the value of *uaddr matches val
  if(*uaddr == val) {
    // Create a new futex element and initialize it.
    struct futex_element e;
    bool enable_timer = false;
    e.uaddr = uaddr;
    e.pthread = NULL;
    e.ms_timeout = ms_timeout;
    e.timedout = false;
    if(e.ms_timeout != (uint64_t)-1) {
      e.ms_timeout += __futex.time;
	  // If we are setting the timeout, get ready to
	  // enable the timer if it is currently disabled.
      if(__futex.timer_enabled == false) {
        __futex.timer_enabled = true;
        enable_timer = true;
      }
    }
    // Insert the futex element into the queue
    TAILQ_INSERT_TAIL(&__futex.queue, &e, link);
    mcs_pdr_unlock(&__futex.lock);

    // Enable the timer if we need to outside the lock
    if(enable_timer)
      futex_wake(&__futex.timer_enabled, 1);

    // Yield the current uthread
    uthread_yield(TRUE, __futex_block, &e);

	// After waking, if we timed out, set the error
	// code appropriately and return
    if(e.timedout) {
      errno = ETIMEDOUT;
      return -1;
    }
  }
  else {
    mcs_pdr_unlock(&__futex.lock);
  }
  return 0;
}

static inline int futex_wake(int *uaddr, int count)
{
  struct futex_element *e,*n = NULL;
  struct futex_queue q = TAILQ_HEAD_INITIALIZER(q);

  // Atomically grab all relevant futex blockers
  // from the global futex queue
  mcs_pdr_lock(&__futex.lock);
  e = TAILQ_FIRST(&__futex.queue);
  while(e != NULL) {
    if(count > 0) {
      n = TAILQ_NEXT(e, link);
      if(e->uaddr == uaddr) {
        TAILQ_REMOVE(&__futex.queue, e, link);
        TAILQ_INSERT_TAIL(&q, e, link);
        count--;
      }
      e = n;
    }
    else break;
  }
  mcs_pdr_unlock(&__futex.lock);

  // Unblock them outside the lock
  e = TAILQ_FIRST(&q);
  while(e != NULL) {
    n = TAILQ_NEXT(e, link);
    TAILQ_REMOVE(&q, e, link);
    while(e->pthread == NULL)
      cpu_relax();
    uthread_runnable((struct uthread*)e->pthread);
    e = n;
  }
  return 0;
}

static void *timer_thread(void *arg)
{
  struct futex_element *e,*n = NULL;
  struct futex_queue q = TAILQ_HEAD_INITIALIZER(q);

  // Do this forever...
  for(;;) {
    // Block for 1 millisecond
    sys_block(1000);

    // Then atomically do the following...
    mcs_pdr_lock(&__futex.lock);
    // Up the time
    __futex.time++;

	// Find all futexes that have timed out on this iteration,
	// and count those still waiting
    int waiting = 0;
    e = TAILQ_FIRST(&__futex.queue);
    while(e != NULL) {
      n = TAILQ_NEXT(e, link);
      if(e->ms_timeout == __futex.time) {
        e->timedout = true;
        TAILQ_REMOVE(&__futex.queue, e, link);
        TAILQ_INSERT_TAIL(&q, e, link);
      }
      else if(e->ms_timeout != (uint64_t)-1)
        waiting++;
      e = n;
    }
    // If there are no more waiting, disable the timer
    if(waiting == 0) {
      __futex.time = 0;
      __futex.timer_enabled = false;
    }
    mcs_pdr_unlock(&__futex.lock);

    // Unblock any futexes that have timed out outside the lock
    e = TAILQ_FIRST(&q);
    while(e != NULL) {
      n = TAILQ_NEXT(e, link);
      TAILQ_REMOVE(&q, e, link);
      while(e->pthread == NULL)
        cpu_relax();
      uthread_runnable((struct uthread*)e->pthread);
      e = n;
    }

    // If we have disabled the timer, park this thread
    futex_wait(&__futex.timer_enabled, false, -1);
  }
}

int futex(int *uaddr, int op, int val,
          const struct timespec *timeout,
          int *uaddr2, int val3)
{
  uint64_t ms_timeout = (uint64_t)-1;
  assert(uaddr2 == NULL);
  assert(val3 == 0);
  if(timeout != NULL) {
    ms_timeout = timeout->tv_sec*1000 + timeout->tv_nsec/1000000L;
    assert(ms_timeout > 0);
  }

  run_once(futex_init());
  switch(op) {
    case FUTEX_WAIT:
      return futex_wait(uaddr, val, ms_timeout);
    case FUTEX_WAKE:
      return futex_wake(uaddr, val);
    default:
      errno = ENOSYS;
      return -1;
  }
  return -1;
}

