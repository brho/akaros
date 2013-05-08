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
  struct mcs_pdr_qnode *mcs_qnode;
};
TAILQ_HEAD(futex_queue, futex_element);

struct futex_data {
  struct mcs_pdr_lock lock;
  struct futex_queue queue;
  struct kmem_cache *element_cache;
};
static struct futex_data __futex;

static inline void futex_init()
{
  mcs_pdr_init(&__futex.lock);
  TAILQ_INIT(&__futex.queue);
  __futex.element_cache = kmem_cache_create("futex_element_cache", 
    sizeof(struct futex_element), __alignof__(struct futex_element),
    0, NULL, NULL);
}

static void __futex_block(struct uthread *uthread, void *arg) {
  struct futex_element *e = (struct futex_element*)arg;
  e->pthread = (pthread_t)uthread;
	__pthread_generic_yield(e->pthread);
  e->pthread->state = PTH_BLK_MUTEX;
  TAILQ_INSERT_TAIL(&__futex.queue, e, link);
  mcs_pdr_unlock(&__futex.lock, e->mcs_qnode);
}

static inline int futex_wait(int *uaddr, int val)
{
  struct mcs_pdr_qnode qnode;
  mcs_pdr_lock(&__futex.lock, &qnode);
  if(*uaddr == val) {
    // We unlock in the body of __futex_block
    struct futex_element *e = kmem_cache_alloc(__futex.element_cache, 0); 
    e->uaddr = uaddr;
    e->mcs_qnode = &qnode;
    uthread_yield(TRUE, __futex_block, e);
  }
  else {
    mcs_pdr_unlock(&__futex.lock, &qnode);
  }
  return 0;
}

static inline int futex_wake(int *uaddr, int count)
{
  struct futex_element *e,*n = NULL;
  struct mcs_pdr_qnode qnode;
  mcs_pdr_lock(&__futex.lock, &qnode);
  e = TAILQ_FIRST(&__futex.queue);
  while(e != NULL) {
    if(count > 0) {
      n = TAILQ_NEXT(e, link);
      if(e->uaddr == uaddr) {
        TAILQ_REMOVE(&__futex.queue, e, link);
        uthread_runnable((struct uthread*)e->pthread);
        kmem_cache_free(__futex.element_cache, e); 
        count--;
      }
      e = n;
    }
    else break;
  }
  mcs_pdr_unlock(&__futex.lock, &qnode);
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

