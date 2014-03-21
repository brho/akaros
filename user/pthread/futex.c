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
#include <alarm.h>

static inline int futex_wake(int *uaddr, int count);
static inline int futex_wait(int *uaddr, int val, uint64_t ms_timeout);
static void *timer_thread(void *arg);

struct futex_element {
  TAILQ_ENTRY(futex_element) link;
  pthread_t pthread;
  int *uaddr;
  uint64_t us_timeout;
  struct alarm_waiter awaiter;
  bool timedout;
};
TAILQ_HEAD(futex_queue, futex_element);

struct futex_data {
  struct mcs_pdr_lock lock;
  struct futex_queue queue;
};
static struct futex_data __futex;

static inline void futex_init()
{
  mcs_pdr_init(&__futex.lock);
  TAILQ_INIT(&__futex.queue);
}

static void __futex_timeout(struct alarm_waiter *awaiter) {
  struct futex_element *__e = NULL;
  struct futex_element *e = (struct futex_element*)awaiter->data;
  //printf("timeout fired: %p\n", e->uaddr);

  // Atomically remove the timed-out element from the futex queue if we won the
  // race against actually completing.
  mcs_pdr_lock(&__futex.lock);
  TAILQ_FOREACH(__e, &__futex.queue, link)
    if (__e == e) break;
  if (__e != NULL)
    TAILQ_REMOVE(&__futex.queue, e, link);
  mcs_pdr_unlock(&__futex.lock);

  // If we removed it, restart it outside the lock
  if (__e != NULL) {
    e->timedout = true;
    //printf("timeout: %p\n", e->uaddr);
    uthread_runnable((struct uthread*)e->pthread);
  }
  // Set this as the very last thing we do whether we successfully woke the
  // thread blocked on the futex or not.  Either we set this or wake() sets
  // this, not both.  Spin on this in the bottom-half of the wait() code to
  // ensure there are no more references to awaiter before freeing the memory
  // for it.
  e->awaiter.data = NULL;
}

static void __futex_block(struct uthread *uthread, void *arg) {
  pthread_t pthread = (pthread_t)uthread;
  struct futex_element *e = (struct futex_element*)arg;

  // Set the remaining properties of the futex element
  e->pthread = pthread;
  e->timedout = false;

  // Insert the futex element into the queue
  TAILQ_INSERT_TAIL(&__futex.queue, e, link);

  // Set an alarm for the futex timeout if applicable
  if(e->us_timeout != (uint64_t)-1) {
    e->awaiter.data = e;
    init_awaiter(&e->awaiter, __futex_timeout);
    set_awaiter_rel(&e->awaiter, e->us_timeout);
    //printf("timeout set: %p\n", e->uaddr);
    set_alarm(&e->awaiter);
  }

  // Notify the scheduler of the type of yield we did
  __pthread_generic_yield(pthread);
  pthread->state = PTH_BLK_MUTEX;

  // Unlock the pdr_lock 
  mcs_pdr_unlock(&__futex.lock);
}

static inline int futex_wait(int *uaddr, int val, uint64_t us_timeout)
{
  // Atomically do the following...
  mcs_pdr_lock(&__futex.lock);
  // If the value of *uaddr matches val
  if(*uaddr == val) {
    //printf("wait: %p, %d\n", uaddr, us_timeout);
    // Create a new futex element and initialize it.
    struct futex_element e;
    e.uaddr = uaddr;
    e.us_timeout = us_timeout;
    // Yield the uthread...
    // We set the remaining properties of the futex element, set the timeout
    // timer, and unlock the pdr lock on the other side.  It is important that
    // we do the unlock on the other side, because (unlike linux, etc.) its
    // possible to get interrupted and drop into vcore context right after
    // releasing the lock.  If that vcore code then calls futex_wake(), we
    // would be screwed.  Doing things this way means we have to hold the lock
    // longer, but its necessary for correctness.
    uthread_yield(TRUE, __futex_block, &e);
    // We are unlocked here!

    // If this futex had a timeout, spin briefly to make sure that all
    // references to e are gone between the wake() and the timeout() code. We
    // use e.awaiter.data to do this.
    if(e.us_timeout != (uint64_t)-1)
      while (e.awaiter.data != NULL)
        cpu_relax();

    // After waking, if we timed out, set the error
    // code appropriately and return
    if(e.timedout) {
      errno = ETIMEDOUT;
      return -1;
    }
  } else {
      mcs_pdr_unlock(&__futex.lock);
  }
  return 0;
}

static inline int futex_wake(int *uaddr, int count)
{
  int max = count;
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
    // Cancel the timeout if one was set
    if(e->us_timeout != (uint64_t)-1) {
      // Try and unset the alarm.  If this fails, then we have already
      // started running the alarm callback.  If it succeeds, then we can
      // set awaiter->data to NULL so that the bottom half of wake can
      // proceed. Either we set awaiter->data to NULL or __futex_timeout
      // does. The fact that we made it here though, means that WE are the
      // one who removed e from the queue, so we are basically just
      // deciding who should set awaiter->data to NULL to indicate that
      // there are no more references to it.
      if(unset_alarm(&e->awaiter)) {
        //printf("timeout canceled: %p\n", e->uaddr);
        e->awaiter.data = NULL;
      }
    }
    //printf("wake: %p\n", uaddr);
    uthread_runnable((struct uthread*)e->pthread);
    e = n;
  }
  return max-count;
}

int futex(int *uaddr, int op, int val,
          const struct timespec *timeout,
          int *uaddr2, int val3)
{
  // Round to the nearest micro-second
  uint64_t us_timeout = (uint64_t)-1;
  assert(uaddr2 == NULL);
  assert(val3 == 0);
  if(timeout != NULL) {
    us_timeout = timeout->tv_sec*1000000L + timeout->tv_nsec/1000L;
    assert(us_timeout > 0);
  }

  run_once(futex_init());
  switch(op) {
    case FUTEX_WAIT:
      return futex_wait(uaddr, val, us_timeout);
    case FUTEX_WAKE:
      return futex_wake(uaddr, val);
    default:
      errno = ENOSYS;
      return -1;
  }
  return -1;
}

