#include <pthread.h>
#include <hart.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

int threads_active = 1;
hart_lock_t work_queue_lock = HART_LOCK_INIT;
pthread_t work_queue_head = 0;
pthread_t work_queue_tail = 0;
pthread_once_t init_once = PTHREAD_ONCE_INIT;
pthread_t active_threads[HART_MAX_MAX_HARTS] = {0};

void queue_insert(pthread_t* head, pthread_t* tail, pthread_t node)
{
  node->next = 0;
  if(*head == 0)
    *head = node;
  else
    (*tail)->next = node;
  *tail = node;
}

pthread_t queue_remove(pthread_t* head, pthread_t* tail)
{
  pthread_t node = *head;
  *head = (*head)->next;
  if(*head == 0)
    *tail = 0;
  node->next = 0;
  return node;
}

void hart_entry()
{
  pthread_t node = NULL;
  while(1)
  {
    hart_lock_lock(&work_queue_lock);
    if(work_queue_head)
      node = queue_remove(&work_queue_head,&work_queue_tail);
    hart_lock_unlock(&work_queue_lock);

    if(node)
      break;
    hart_relax();
  }

  active_threads[hart_self()] = node;

  pthread_exit(node->start_routine(node->arg));
}

void _pthread_init()
{
  // if we allocated active_threads dynamically, we'd do so here
}

int pthread_attr_init(pthread_attr_t *a)
{
  return 0;
}

int pthread_attr_destroy(pthread_attr_t *a)
{
  return 0;
}

int pthread_create(pthread_t* thread, const pthread_attr_t* attr,
                   void *(*start_routine)(void *), void* arg)
{
  pthread_once(&init_once,&_pthread_init);

  *thread = (pthread_t)malloc(sizeof(work_queue_t));
  (*thread)->start_routine = start_routine;
  (*thread)->arg = arg;
  (*thread)->next = 0;
  (*thread)->finished = 0;
  (*thread)->detached = 0;

  hart_lock_lock(&work_queue_lock);
  {
    threads_active++;
    queue_insert(&work_queue_head,&work_queue_tail,*thread);
    // don't return until we get a hart
    while(threads_active > hart_current_harts() && hart_request(1));
  }
  hart_lock_unlock(&work_queue_lock);

  return 0;
}

int pthread_join(pthread_t t, void** arg)
{
  volatile pthread_t thread = t;
  while(!thread->finished);
  if(arg) *arg = thread->arg;
  free(thread);
  return 0;
}

int pthread_mutexattr_init(pthread_mutexattr_t* attr)
{
  attr->type = PTHREAD_MUTEX_DEFAULT;
  return 0;
}

int pthread_mutexattr_destroy(pthread_mutexattr_t* attr)
{
  return 0;
}

int pthread_mutexattr_gettype(const pthread_mutexattr_t* attr, int* type)
{
  *type = attr ? attr->type : PTHREAD_MUTEX_DEFAULT;
  return 0;
}

int pthread_mutexattr_settype(pthread_mutexattr_t* attr, int type)
{
  if(type != PTHREAD_MUTEX_NORMAL)
    return -EINVAL;
  attr->type = type;
  return 0;
}

int pthread_mutex_init(pthread_mutex_t* m, const pthread_mutexattr_t* attr)
{
  m->attr = attr;
  m->lock = 0;
  return 0;
}

int pthread_mutex_lock(pthread_mutex_t* m)
{
  while(pthread_mutex_trylock(m))
    while(*(volatile size_t*)&m->lock);
  return 0;
}

int pthread_mutex_trylock(pthread_mutex_t* m)
{
  return hart_swap(&m->lock,1) == 0 ? 0 : -EBUSY;
}

int pthread_mutex_unlock(pthread_mutex_t* m)
{
  m->lock = 0;
  return 0;
}

int pthread_mutex_destroy(pthread_mutex_t* m)
{
  return 0;
}

int pthread_cond_init(pthread_cond_t *c, const pthread_condattr_t *a)
{
  c->attr = a;
  memset(c->waiters,0,sizeof(c->waiters));
  return 0;
}

int pthread_cond_destroy(pthread_cond_t *c)
{
  return 0;
}

int pthread_cond_broadcast(pthread_cond_t *c)
{
  memset(c->waiters,0,sizeof(c->waiters));
  return 0;
}

int pthread_cond_signal(pthread_cond_t *c)
{
  int i;
  for(i = 0; i < hart_max_harts(); i++)
  {
    if(c->waiters[i])
    {
      c->waiters[i] = 0;
      break;
    }
  }
  return 0;
}

int pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m)
{
  c->waiters[hart_self()] = 1;
  pthread_mutex_unlock(m);

  volatile int* poll = &c->waiters[hart_self()];
  while(*poll);

  pthread_mutex_lock(m);

  return 0;
}

int pthread_condattr_init(pthread_condattr_t *a)
{
  a = PTHREAD_PROCESS_PRIVATE;
  return 0;
}

int pthread_condattr_destroy(pthread_condattr_t *a)
{
  return 0;
}

int pthread_condattr_setpshared(pthread_condattr_t *a, int s)
{
  a->pshared = s;
  return 0;
}

int pthread_condattr_getpshared(pthread_condattr_t *a, int *s)
{
  *s = a->pshared;
  return 0;
}

pthread_t pthread_self()
{
  return active_threads[hart_self()];
}

int pthread_equal(pthread_t t1, pthread_t t2)
{
  return t1 == t2;
}

void pthread_exit(void* ret)
{
  pthread_once(&init_once,&_pthread_init);

  pthread_t t = pthread_self();

  hart_lock_lock(&work_queue_lock);
  threads_active--;
  if(threads_active == 0)
    exit(0);
  hart_lock_unlock(&work_queue_lock);

  if(t)
  {
    t->arg = ret;
    t->finished = 1;
    if(t->detached)
      free(t);
  }

  hart_entry();
}

int pthread_once(pthread_once_t* once_control, void (*init_routine)(void))
{
  if(hart_swap(once_control,1) == 0)
    init_routine();
  return 0;
}

int pthread_barrier_init(pthread_barrier_t* b, const pthread_barrierattr_t* a, int count)
{
  memset(b->local_sense,0,sizeof(b->local_sense));

  b->sense = 0;
  b->nprocs = b->count = count;
  hart_lock_init(&b->lock);
  return 0;
}

int pthread_barrier_wait(pthread_barrier_t* b)
{
  int id = hart_self();
  int ls = b->local_sense[32*id] = 1 - b->local_sense[32*id];

  hart_lock_lock(&b->lock);
  int count = --b->count;
  hart_lock_unlock(&b->lock);

  if(count == 0)
  {
    b->count = b->nprocs;
    b->sense = ls;
    return PTHREAD_BARRIER_SERIAL_THREAD;
  }
  else
  {
    while(b->sense != ls);
    return 0;
  }
}

int pthread_barrier_destroy(pthread_barrier_t* b)
{
  return 0;
}
