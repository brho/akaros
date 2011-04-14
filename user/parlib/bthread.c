#include <bthread.h>
#include <vcore.h>
#include <mcs.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include <arch/atomic.h>

int threads_active = 1;
mcs_lock_t work_queue_lock = MCS_LOCK_INIT;
bthread_t work_queue_head = 0;
bthread_t work_queue_tail = 0;
bthread_once_t init_once = BTHREAD_ONCE_INIT;
bthread_t active_threads[MAX_VCORES] = {0};

void queue_insert(bthread_t* head, bthread_t* tail, bthread_t node)
{
  node->next = 0;
  if(*head == 0)
    *head = node;
  else
    (*tail)->next = node;
  *tail = node;
}

bthread_t queue_remove(bthread_t* head, bthread_t* tail)
{
  bthread_t node = *head;
  *head = (*head)->next;
  if(*head == 0)
    *tail = 0;
  node->next = 0;
  return node;
}

void vcore_entry()
{
  struct mcs_lock_qnode local_qn = {0};
  bthread_t node = NULL;
  while(1)
  {
    mcs_lock_lock(&work_queue_lock, &local_qn);
    if(work_queue_head)
      node = queue_remove(&work_queue_head,&work_queue_tail);
    mcs_lock_unlock(&work_queue_lock, &local_qn);

    if(node)
      break;
    cpu_relax();
  }

  active_threads[vcore_id()] = node;

  bthread_exit(node->start_routine(node->arg));
}

void _bthread_init()
{
  // if we allocated active_threads dynamically, we'd do so here
}

int bthread_attr_init(bthread_attr_t *a)
{
  return 0;
}

int bthread_attr_destroy(bthread_attr_t *a)
{
  return 0;
}

int bthread_create(bthread_t* thread, const bthread_attr_t* attr,
                   void *(*start_routine)(void *), void* arg)
{
  struct mcs_lock_qnode local_qn = {0};
  bthread_once(&init_once,&_bthread_init);

  *thread = (bthread_t)malloc(sizeof(work_queue_t));
  (*thread)->start_routine = start_routine;
  (*thread)->arg = arg;
  (*thread)->next = 0;
  (*thread)->finished = 0;
  (*thread)->detached = 0;
  mcs_lock_lock(&work_queue_lock, &local_qn);
  {
    threads_active++;
    queue_insert(&work_queue_head,&work_queue_tail,*thread);
    // don't return until we get a vcore
    while(threads_active > num_vcores() && vcore_request(1));
  }
  mcs_lock_unlock(&work_queue_lock, &local_qn);

  return 0;
}

int bthread_join(bthread_t t, void** arg)
{
  volatile bthread_t thread = t;
  while(!thread->finished);
  if(arg) *arg = thread->arg;
  free(thread);
  return 0;
}

int bthread_mutexattr_init(bthread_mutexattr_t* attr)
{
  attr->type = BTHREAD_MUTEX_DEFAULT;
  return 0;
}

int bthread_mutexattr_destroy(bthread_mutexattr_t* attr)
{
  return 0;
}


int bthread_attr_setdetachstate(bthread_attr_t *__attr,int __detachstate) {
	*__attr = __detachstate;
	return 0;
}

int bthread_mutexattr_gettype(const bthread_mutexattr_t* attr, int* type)
{
  *type = attr ? attr->type : BTHREAD_MUTEX_DEFAULT;
  return 0;
}

int bthread_mutexattr_settype(bthread_mutexattr_t* attr, int type)
{
  if(type != BTHREAD_MUTEX_NORMAL)
    return EINVAL;
  attr->type = type;
  return 0;
}

int bthread_mutex_init(bthread_mutex_t* m, const bthread_mutexattr_t* attr)
{
  m->attr = attr;
  m->lock = 0;
  return 0;
}

int bthread_mutex_lock(bthread_mutex_t* m)
{
  while(bthread_mutex_trylock(m))
    while(*(volatile size_t*)&m->lock);
  return 0;
}

int bthread_mutex_trylock(bthread_mutex_t* m)
{
  return atomic_swap(&m->lock,1) == 0 ? 0 : EBUSY;
}

int bthread_mutex_unlock(bthread_mutex_t* m)
{
  m->lock = 0;
  return 0;
}

int bthread_mutex_destroy(bthread_mutex_t* m)
{
  return 0;
}

int bthread_cond_init(bthread_cond_t *c, const bthread_condattr_t *a)
{
  c->attr = a;
  memset(c->waiters,0,sizeof(c->waiters));
  return 0;
}

int bthread_cond_destroy(bthread_cond_t *c)
{
  return 0;
}

int bthread_cond_broadcast(bthread_cond_t *c)
{
  memset(c->waiters,0,sizeof(c->waiters));
  return 0;
}

int bthread_cond_signal(bthread_cond_t *c)
{
  int i;
  for(i = 0; i < max_vcores(); i++)
  {
    if(c->waiters[i])
    {
      c->waiters[i] = 0;
      break;
    }
  }
  return 0;
}

int bthread_cond_wait(bthread_cond_t *c, bthread_mutex_t *m)
{
  c->waiters[vcore_id()] = 1;
  bthread_mutex_unlock(m);

  volatile int* poll = &c->waiters[vcore_id()];
  while(*poll);

  bthread_mutex_lock(m);

  return 0;
}

int bthread_condattr_init(bthread_condattr_t *a)
{
  a = BTHREAD_PROCESS_PRIVATE;
  return 0;
}

int bthread_condattr_destroy(bthread_condattr_t *a)
{
  return 0;
}

int bthread_condattr_setpshared(bthread_condattr_t *a, int s)
{
  a->pshared = s;
  return 0;
}

int bthread_condattr_getpshared(bthread_condattr_t *a, int *s)
{
  *s = a->pshared;
  return 0;
}

bthread_t bthread_self()
{
  return active_threads[vcore_id()];
}

int bthread_equal(bthread_t t1, bthread_t t2)
{
  return t1 == t2;
}

void bthread_exit(void* ret)
{
  struct mcs_lock_qnode local_qn = {0};
  bthread_once(&init_once,&_bthread_init);

  bthread_t t = bthread_self();

  mcs_lock_lock(&work_queue_lock, &local_qn);
  threads_active--;
  if(threads_active == 0)
    exit(0);
  mcs_lock_unlock(&work_queue_lock, &local_qn);

  if(t)
  {
    t->arg = ret;
    t->finished = 1;
    if(t->detached)
      free(t);
  }

  vcore_entry();
}

int bthread_once(bthread_once_t* once_control, void (*init_routine)(void))
{
  if(atomic_swap(once_control,1) == 0)
    init_routine();
  return 0;
}

int bthread_barrier_init(bthread_barrier_t* b, const bthread_barrierattr_t* a, int count)
{
  struct mcs_lock_qnode local_qn = {0};
  memset(b->local_sense,0,sizeof(b->local_sense));

  b->sense = 0;
  b->nprocs = b->count = count;
  mcs_lock_init(&b->lock);
  return 0;
}

int bthread_barrier_wait(bthread_barrier_t* b)
{
  struct mcs_lock_qnode local_qn = {0};
  int id = vcore_id();
  int ls = b->local_sense[32*id] = 1 - b->local_sense[32*id];

  mcs_lock_lock(&b->lock, &local_qn);
  int count = --b->count;
  mcs_lock_unlock(&b->lock, &local_qn);

  if(count == 0)
  {
    b->count = b->nprocs;
    b->sense = ls;
    return BTHREAD_BARRIER_SERIAL_THREAD;
  }
  else
  {
    while(b->sense != ls);
    return 0;
  }
}

int bthread_barrier_destroy(bthread_barrier_t* b)
{
  return 0;
}
