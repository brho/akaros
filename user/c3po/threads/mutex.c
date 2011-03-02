
/**
 * Simple mutexes, rwlocks and condition variables
 **/

#include "threadlib_internal.h"
#include "threadlib.h"

#include "util.h"
#include <assert.h>
#include <error.h>
#include <stdlib.h>
#include <sys/time.h>

#ifndef DEBUG_mutex_c
#undef debug
#define debug(...)
#undef tdebug
#define tdebug(...)
#endif


static void queue_init(queue_t *q, int n);
static void queue_free(queue_t *q) __attribute__ ((unused));
static void enqueue(queue_t *q, void *v);
static void *dequeue(queue_t *q);
static int queue_isempty(queue_t *q);
static int queue_remove(queue_t *q, void *v);

inline int thread_mutex_init(mutex_t *m, char *name) 
{
  m->state = UNLOCKED;
  m->name = name;
  m->count = 0;
  m->owner = NULL;
  queue_init(&m->wait_queue, 0);   // This will be allocated when a thread actually waits on the mutex
  thread_latch_init( m->latch );
  return TRUE;
}


static inline int _thread_mutex_lock(mutex_t *m, int istry)
{
  if (m == NULL)
    return_errno(FALSE, EINVAL);

  thread_latch( m->latch );

  // If unlocked, just lock it
  if (m->state == UNLOCKED)
    {
      // Need to have a spin lock on the mutex if we have multiple kernel threads
      debug("thread_mutex_lock: locking mutex directly\n");
      m->state = LOCKED;
      m->owner = current_thread;
      m->count = 1;
      thread_unlatch( m->latch );
      return TRUE;
    }
    
  assert(m->count >= 1);

  // already locked by caller
  if (m->owner == current_thread)
    {
      // recursive lock
      m->count++;
      debug("thread_mutex_lock: recursive locking\n");
      thread_unlatch( m->latch );
      return TRUE;
    }

  // Locked by someone else.  
  if (istry)
    return_errno_unlatch(FALSE, EBUSY, m->latch);

  // add current thread to waiting queue
  assert(current_thread);
  enqueue(&m->wait_queue, current_thread);

  // suspend the current thread
  thread_unlatch( m->latch );
  CAP_SET_SYSCALL();
  thread_suspend_self(0);
  CAP_CLEAR_SYSCALL();

  // wake up.  Verify that we now own the lock
  thread_latch( m->latch );
  assert(m->state == LOCKED);
  assert(m->owner == current_thread);
  assert(m->count == 1);
  thread_unlatch( m->latch );

  return TRUE;
}

inline int thread_mutex_lock(mutex_t *m)
{
  return _thread_mutex_lock(m, FALSE);
}

inline int thread_mutex_trylock(mutex_t *m)
{
  return _thread_mutex_lock(m, TRUE);
}

inline int thread_mutex_unlock(mutex_t *m)
{
  thread_t *t;

  if (m == NULL)
    return_errno(FALSE, EINVAL);

  thread_latch(m->latch);
  if (m->state != LOCKED) 
    return_errno_unlatch(FALSE, EDEADLK, m->latch);
  if (m->owner != current_thread)
    return_errno_unlatch(FALSE, EPERM, m->latch);

  m->count--;

  // still locked by the current thread
  if (m->count > 0)
    return_errno_unlatch(TRUE, 0, m->latch);

  // get the first waiter
  t = (thread_t *)dequeue(&m->wait_queue);

  // no threads waiting
  if(t == NULL) {
    m->state = UNLOCKED;
    m->count = 0;
    m->owner = NULL;
  }

  // resume the waiter
  else {
    m->owner = t;
    m->count = 1;
    thread_resume(t);
  }

  thread_unlatch(m->latch);
  return TRUE;
}


/*
**  Read-Write Locks
*/

int thread_rwlock_init(rwlock_t *rwlock)
{
    if (rwlock == NULL)
        return_errno(FALSE, EINVAL);
    rwlock->rw_state = THREAD_RWLOCK_INITIALIZED;
    rwlock->rw_readers = 0;
    thread_mutex_init(&rwlock->rw_mutex_rd, NULL);
    thread_mutex_init(&rwlock->rw_mutex_rw, NULL);
    return TRUE;
}

static int _thread_rwlock_lock(rwlock_t *rwlock, int op, int tryonly)
{
    /* consistency checks */
    if (rwlock == NULL)
        return_errno(FALSE, EINVAL);
    if (!(rwlock->rw_state & THREAD_RWLOCK_INITIALIZED))
        return_errno(FALSE, EDEADLK);

    /* lock lock */
    if (op == RWLOCK_RW) {
        /* read-write lock is simple */
      if (!_thread_mutex_lock(&(rwlock->rw_mutex_rw), tryonly))
	return FALSE;
      rwlock->rw_mode = RWLOCK_RW;
    }
    else {
        /* read-only lock is more complicated to get right */
        if (!_thread_mutex_lock(&(rwlock->rw_mutex_rd), tryonly))
            return FALSE;
        rwlock->rw_readers++;
        if (rwlock->rw_readers == 1) {
            if (!_thread_mutex_lock(&(rwlock->rw_mutex_rw), tryonly)) {
                rwlock->rw_readers--;
                thread_mutex_unlock(&(rwlock->rw_mutex_rd));
                return FALSE;
            }
        }
        rwlock->rw_mode = RWLOCK_RD;
        thread_mutex_unlock(&(rwlock->rw_mutex_rd));
    }
    return TRUE;
}

int thread_rwlock_lock(rwlock_t *l, int op)
{
  return _thread_rwlock_lock(l, op, FALSE);
}

int thread_rwlock_trylock(rwlock_t *l, int op)
{
  return _thread_rwlock_lock(l, op, TRUE);
}

int thread_rwlock_unlock(rwlock_t *rwlock)
{
    /* consistency checks */
    if (rwlock == NULL)
        return_errno(FALSE, EINVAL);
    if (!(rwlock->rw_state & THREAD_RWLOCK_INITIALIZED))
        return_errno(FALSE, EDEADLK);

    /* unlock lock */
    if (rwlock->rw_mode == RWLOCK_RW) {
        /* read-write unlock is simple */
        if (!thread_mutex_unlock(&(rwlock->rw_mutex_rw)))
            return FALSE;
    }
    else {
        /* read-only unlock is more complicated to get right */
        if (!_thread_mutex_lock(&(rwlock->rw_mutex_rd), FALSE))
            return FALSE;
        rwlock->rw_readers--;
        if (rwlock->rw_readers == 0) {
            if (!thread_mutex_unlock(&(rwlock->rw_mutex_rw))) {
                rwlock->rw_readers++;
                thread_mutex_unlock(&(rwlock->rw_mutex_rd));
                return FALSE;
            }
        }
        rwlock->rw_mode = RWLOCK_RD;
        thread_mutex_unlock(&(rwlock->rw_mutex_rd));
    }
    return TRUE;
}

/*
**  Condition Variables
*/

int thread_cond_init(cond_t *cond)
{
    if (cond == NULL)
        return_errno(FALSE, EINVAL);
    cond->cn_state   = THREAD_COND_INITIALIZED;
    cond->cn_waiters = 0;
    queue_init(&cond->wait_queue, 0);
    return TRUE;
}

int thread_cond_timedwait(cond_t *cond, mutex_t *mutex, const struct timespec *abstime)
{
  unsigned long timeout = 0;
  int sus_rv = 0;
    /* consistency checks */
    if (cond == NULL || mutex == NULL)
        return_errno(FALSE, EINVAL);
    if (!(cond->cn_state & THREAD_COND_INITIALIZED))
        return_errno(FALSE, EDEADLK);

    /* add us to the number of waiters */
    cond->cn_waiters++;

    /* unlock mutex (caller had to lock it first) */
    thread_mutex_unlock(mutex);

    /* wait until the condition is signaled */
    assert (current_thread);
    enqueue(&cond->wait_queue, current_thread);

    if (abstime) {
      struct timeval tv;
      gettimeofday(&tv, NULL);
      timeout = (abstime->tv_sec - tv.tv_sec) * 1000000 +
	(abstime->tv_nsec / 1000 - tv.tv_usec);
    }

    CAP_SET_SYSCALL();
    sus_rv = thread_suspend_self(timeout);
    CAP_CLEAR_SYSCALL();
    
    /* relock mutex */
    thread_mutex_lock(mutex);

  	// FIXME: this is wrong, we should retry after INTERRUPTED
    if (sus_rv == TIMEDOUT || sus_rv == INTERRUPTED) {
      /* we timed out */
      /* remove us from the number of waiters */
      /* our thread could possibly be already removed by thread_cond_signal() */
      if (queue_remove(&cond->wait_queue, current_thread))
	cond->cn_waiters--;

      return_errno(FALSE, ETIMEDOUT);
    } else
      return TRUE;
}

int thread_cond_wait(cond_t *cond, mutex_t *mutex)
{
  return thread_cond_timedwait(cond, mutex, NULL);
}


static int _thread_cond_signal(cond_t *cond, int broadcast)
{
    /* consistency checks */
    if (cond == NULL)
        return_errno(FALSE, EINVAL);
    if (!(cond->cn_state & THREAD_COND_INITIALIZED))
        return_errno(FALSE, EDEADLK);

    // do something only if there is at least one waiters (POSIX semantics)
    if (cond->cn_waiters > 0) {
      // signal the condition
      do {
	thread_t *t = dequeue(&cond->wait_queue);
	assert (t != NULL);
	thread_resume(t);   // t could also be a timed out thread, but it doesn't matter
	cond->cn_waiters--;
      } while (broadcast && !queue_isempty(&cond->wait_queue));
      
      // and give other threads a chance to grab the CPU 
      CAP_SET_SYSCALL();
      thread_yield();
      CAP_CLEAR_SYSCALL();
    }

    /* return to caller */
    return TRUE;
}

int thread_cond_signal(cond_t *cond) {
  return _thread_cond_signal(cond, FALSE);
}

int thread_cond_broadcast(cond_t *cond) {
  return _thread_cond_signal(cond, TRUE);
}

// Simple queue implementation
#define WRAP_LEN(head, tail, n) (head) <= (tail) ? ((tail) - (head)) \
                                : ((n) + (tail) - (head))
#define WRAP_DEC(x, n) ((x) == 0 ? (n) - 1 : (x) - 1)
#define WRAP_INC(x, n) ((x) == ((n) - 1) ? 0 : (x) + 1)

static void queue_init(queue_t *q, int n) {
  q->size = n + 1;
  q->data = (void **)malloc((n + 1) * sizeof(void *));
  assert(q->data);
  q->head = 0;
  q->tail = 0;
}

static void queue_free(queue_t *q) {
  free(q->data);
}

static void enqueue(queue_t *q, void *v) {
  int cur_size = WRAP_LEN(q->head, q->tail, q->size);
  if (cur_size == q->size - 1) {

    /* Enlarge the queue */
    int newsize;
    if (q->size == 0)
      newsize = 2;
    else 
      newsize = q->size + q->size;

    q->data = realloc(q->data, newsize * sizeof(void *));
    assert(q->data);
    if (q->head > q->tail) {   /* do we need to move data? */
      memmove(q->data + newsize - (q->size - q->head), q->data + q->head, (q->size - q->head)
	      * sizeof(void *));  
      q->head = newsize - (q->size - q->head);
      assert(*(q->data + newsize - 1) == *(q->data + q->size - 1) ); // the old last element should now be at the end of the larger block
    }
    q->size = newsize;
  }
  *(q->data + q->tail) = v;
  q->tail = (q->tail == q->size - 1) ? 0 : q->tail + 1;
}

static void *dequeue(queue_t *q) {
  void *r;

  // the queue is empty
  if(q->head == q->tail) 
    return NULL;

  r = *(q->data + q->head);
  q->head = (q->head == q->size - 1) ? 0 : q->head + 1;
  return r;
}

// remove the first occurance of a certain value from the queue
// return TRUE if the value is found
// FIXME: this is O(N)!
static int queue_remove(queue_t *q, void *v) {
  int i = q->head;
  int rv = FALSE;

  while (i != q->tail) {
    if (q->data[i] == v) {
      q->tail = WRAP_DEC(q->tail, q->size);
      q->data[i] = q->data[q->tail];
      rv = TRUE;
      break;
    }
    i = WRAP_INC(i, q->size);
  }

  return rv;
}

static int queue_isempty(queue_t *q) {
  return q->head == q->tail;
}

#undef WRAP_LEN

