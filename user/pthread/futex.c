#include <ros/common.h>
#include <futex.h>
#include <sys/queue.h>
#include <pthread.h>
#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include <slab.h>
#include <mcs.h>

struct futex_element {
  TAILQ_ENTRY(futex_element) link;
  pthread_t pthread;
  int *uaddr;
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

static void __futex_block(struct uthread *uthread, void *arg) {
  pthread_t pthread = (pthread_t)uthread;
  struct futex_element *e = (struct futex_element*)arg;
  __pthread_generic_yield(pthread);
  pthread->state = PTH_BLK_MUTEX;
  e->pthread = pthread;
}

static inline int futex_wait(int *uaddr, int val)
{
  mcs_pdr_lock(&__futex.lock);
  if(*uaddr == val) {
    struct futex_element e;
    e.uaddr = uaddr;
    e.pthread = NULL;
    TAILQ_INSERT_TAIL(&__futex.queue, &e, link);
    mcs_pdr_unlock(&__futex.lock);
    uthread_yield(TRUE, __futex_block, &e);
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

int futex(int *uaddr, int op, int val, const struct timespec *timeout,
                 int *uaddr2, int val3)
{
  assert(timeout == NULL);
  assert(uaddr2 == NULL);
  assert(val3 == 0);

  run_once(futex_init());
  switch(op) {
    case FUTEX_WAIT:
      return futex_wait(uaddr, val);
    case FUTEX_WAKE:
      return futex_wake(uaddr, val);
    default:
      errno = ENOSYS;
      return -1;
  }
  return -1;
}

