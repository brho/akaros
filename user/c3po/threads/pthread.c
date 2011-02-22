
// pthread.h must be the first file included
#define _PTHREAD_PRIVATE
#include <pthread.h>
#include "threadlib.h"
#include "threadlib_internal.h"
#undef _PTHREAD_PRIVATE

#include <stdlib.h>
#include <errno.h>

#include "util.h"

#ifndef DEBUG_pthread_c
#undef debug
#define debug(...)
#undef tdebug
#define tdebug(...)
#endif

// comment out, to enable debugging in this file
//#define debug(...)

#ifdef OK
#undef OK
#endif
#define OK 0

// Add code to ensure initialization code here
#define pthread_initialize()

/*
**  THREAD ATTRIBUTE ROUTINES
*/

int pthread_attr_init(pthread_attr_t *attr)
{
  thread_attr_t na;
  if (attr == NULL)
    return_errno(EINVAL, EINVAL);
  if ((na = thread_attr_new()) == NULL)
    return errno;
  (*attr) = (pthread_attr_t)na;
  return OK;
}

int pthread_attr_destroy(pthread_attr_t *attr)
{
  thread_attr_t na;
  if (attr == NULL || *attr == NULL)
        return_errno(EINVAL, EINVAL);
    na = (thread_attr_t)(*attr);
    thread_attr_destroy(na);
    *attr = NULL;
    return OK;
}

int pthread_attr_setinheritsched(pthread_attr_t *attr, int inheritsched)
{
    (void) inheritsched;
    if (attr == NULL)
        return_errno(EINVAL, EINVAL);
    /* not supported */
    return_errno(ENOSYS, ENOSYS);
}

int pthread_attr_getinheritsched(const pthread_attr_t *attr, int *inheritsched)
{
    if (attr == NULL || inheritsched == NULL)
        return_errno(EINVAL, EINVAL);
    /* not supported */
    return_errno(ENOSYS, ENOSYS);
}

int pthread_attr_setschedparam(pthread_attr_t *attr, struct sched_param *schedparam)
{
    (void) schedparam;
    if (attr == NULL)
        return_errno(EINVAL, EINVAL);
    /* not supported */
    return_errno(ENOSYS, ENOSYS);
}

int pthread_attr_getschedparam(const pthread_attr_t *attr, struct sched_param *schedparam)
{
    if (attr == NULL || schedparam == NULL)
        return_errno(EINVAL, EINVAL);
    /* not supported */
    return_errno(ENOSYS, ENOSYS);
}

int pthread_attr_setschedpolicy(pthread_attr_t *attr, int schedpolicy)
{
    (void) schedpolicy;
    if (attr == NULL)
        return_errno(EINVAL, EINVAL);
    /* not supported */
    return_errno(ENOSYS, ENOSYS);
}

int pthread_attr_getschedpolicy(const pthread_attr_t *attr, int *schedpolicy)
{
    if (attr == NULL || schedpolicy == NULL)
        return_errno(EINVAL, EINVAL);
    /* not supported */
    return_errno(ENOSYS, ENOSYS);
}

int pthread_attr_setscope(pthread_attr_t *attr, int scope)
{
    (void) scope;
    if (attr == NULL)
        return_errno(EINVAL, EINVAL);
    /* not supported */
    return_errno(ENOSYS, ENOSYS);
}

int pthread_attr_getscope(const pthread_attr_t *attr, int *scope)
{
    if (attr == NULL || scope == NULL)
        return_errno(EINVAL, EINVAL);
    /* not supported */
    return_errno(ENOSYS, ENOSYS);
}

int pthread_attr_setstacksize(pthread_attr_t *attr, size_t stacksize)
{
    (void) stacksize;
    if (attr == NULL)
        return_errno(EINVAL, EINVAL);
    // FIXME: now doing nothing

    return OK;
}

int pthread_attr_getstacksize(const pthread_attr_t *attr, size_t *stacksize)
{
    if (attr == NULL || stacksize == NULL)
        return_errno(EINVAL, EINVAL);

    // FIXME: returning a faked one
    *stacksize = 256 * 1024;
    return OK;
}

// FIXME: implemente this
int pthread_attr_setstackaddr(pthread_attr_t *attr, void *stackaddr)
{
    (void) stackaddr;
    if (attr == NULL)
        return_errno(EINVAL, EINVAL);
    return_errno(ENOSYS, ENOSYS);
}

int pthread_attr_getstackaddr(const pthread_attr_t *attr, void **stackaddr)
{
    if (attr == NULL || stackaddr == NULL)
        return_errno(EINVAL, EINVAL);
    return_errno(ENOSYS, ENOSYS);
}

int pthread_attr_setdetachstate(pthread_attr_t *attr, int detachstate)
{
    int s;

    if (attr == NULL)
        return_errno(EINVAL, EINVAL);
    if (detachstate == PTHREAD_CREATE_DETACHED)
        s = THREAD_CREATE_DETACHED;
    else  if (detachstate == PTHREAD_CREATE_JOINABLE)
        s = THREAD_CREATE_JOINABLE;
    else
        return_errno(EINVAL, EINVAL);
    if (!thread_attr_set((thread_attr_t)(*attr), THREAD_ATTR_JOINABLE, s))
        return errno;
    return OK;
}

int pthread_attr_getdetachstate(const pthread_attr_t *attr, int *detachstate)
{
    int s;

    if (attr == NULL || detachstate == NULL)
        return_errno(EINVAL, EINVAL);
    if (!thread_attr_get((thread_attr_t)(*attr), THREAD_ATTR_JOINABLE, &s))
        return errno;
    if (s == THREAD_CREATE_JOINABLE)
        *detachstate = PTHREAD_CREATE_JOINABLE;
    else
        *detachstate = PTHREAD_CREATE_DETACHED;
    return OK;
}

int pthread_attr_setguardsize(pthread_attr_t *attr, int stacksize)
{
    if (attr == NULL || stacksize < 0)
        return_errno(EINVAL, EINVAL);
    /* not supported */
    return_errno(ENOSYS, ENOSYS);
}

int pthread_attr_getguardsize(const pthread_attr_t *attr, int *stacksize)
{
    if (attr == NULL || stacksize == NULL)
        return_errno(EINVAL, EINVAL);
    /* not supported */
    return_errno(ENOSYS, ENOSYS);
}

int pthread_attr_setname_np(pthread_attr_t *attr, char *name)
{
    if (attr == NULL || name == NULL)
        return_errno(EINVAL, EINVAL);
    notimplemented(pthread_attr_setname_np);
    //    if (!thread_attr_set((thread_attr_t)(*attr), THREAD_ATTR_NAME, name))
    //        return errno;
    return OK;
}

int pthread_attr_getname_np(const pthread_attr_t *attr, char **name)
{
    if (attr == NULL || name == NULL)
        return_errno(EINVAL, EINVAL);
    //    if (!thread_attr_get((thread_attr_t)(*attr), THREAD_ATTR_NAME, name))
    //        return errno;
    notimplemented(pthread_attr_getname_np);
    *name = "FAKE_NAME";
    return OK;
}

int pthread_attr_setprio_np(pthread_attr_t *attr, int prio)
{
  //    if (attr == NULL || (prio < THREAD_PRIO_MIN || prio > THREAD_PRIO_MAX))
  //      return_errno(EINVAL, EINVAL);
    if (!thread_attr_set((thread_attr_t)(*attr), THREAD_ATTR_PRIO, prio))
        return errno;
    return OK;
}

int pthread_attr_getprio_np(const pthread_attr_t *attr, int *prio)
{
    if (attr == NULL || prio == NULL)
        return_errno(EINVAL, EINVAL);
    if (!thread_attr_get((thread_attr_t)(*attr), THREAD_ATTR_PRIO, prio))
        return errno;
    return OK;
}



/*
**  THREAD ROUTINES
*/

int pthread_create(
    pthread_t *thread, const pthread_attr_t *attr,
    void *(*start_routine)(void *), void *arg)
{
    pthread_initialize();
    if (thread == NULL || start_routine == NULL)
        return_errno(EINVAL, EINVAL);
    //    if (thread_ctrl(THREAD_CTRL_GETTHREADS) >= PTHREAD_THREADS_MAX)
    //    return_errno(EAGAIN, EAGAIN);
    if (attr == NULL)
        *thread = (pthread_t)thread_spawn(NULL, start_routine, arg);
    else
        *thread = (pthread_t)thread_spawn_with_attr(NULL, start_routine, arg, (thread_attr_t)(*attr));
    if (*thread == NULL) {
        errno = ENOMEM;
        return -1;
    }
    return OK;
}

int __pthread_detach(pthread_t thread)
{
    thread_attr_t na;

    if (thread == NULL)
        return_errno(EINVAL, EINVAL);
    if ((na = thread_attr_of((thread_t *)thread)) == NULL)
        return errno;
    if (!thread_attr_set(na, THREAD_ATTR_JOINABLE, FALSE))
        return errno;
    thread_attr_destroy(na);
    return OK;
}

int pthread_detach(pthread_t thread)
{
    return __pthread_detach(thread);
}

pthread_t pthread_self(void)
{
    pthread_initialize();
    return (pthread_t)thread_self();
}

int pthread_equal(pthread_t t1, pthread_t t2)
{
    return (t1 == t2);
}

int pthread_yield_np(void)
{
    pthread_initialize();
    thread_yield();
    return OK;
}

int pthread_yield(void)
{
    pthread_initialize();
    thread_yield();
    return OK;
}

void pthread_exit(void *value_ptr)
{
    pthread_initialize();
    thread_exit(value_ptr);
    return;
}

int pthread_join(pthread_t thread, void **value_ptr)
{
    pthread_initialize();
    if (!thread_join((thread_t *)thread, value_ptr))
        return errno;
    //    if (value_ptr != NULL)
    //        if (*value_ptr == THREAD_CANCELED)
    //            *value_ptr = PTHREAD_CANCELED;
    return OK;
}

int pthread_once(
    pthread_once_t *once_control, void (*init_routine)(void))
{
    pthread_initialize();
    if (once_control == NULL || init_routine == NULL)
        return_errno(EINVAL, EINVAL);
    if (*once_control != 1)
        init_routine();
    *once_control = 1;
    return OK;
}
strong_alias(pthread_once,__pthread_once);

int pthread_sigmask(int how, const sigset_t *set, sigset_t *oset)
{
    return sigprocmask(how, set, oset);		// this is our implementation
}

int pthread_kill(pthread_t thread, int sig)
{
    pthread_initialize();
	
//    return thread_kill((thread_t*)thread, sig);
}

/*
**  CONCURRENCY ROUTINES
**  
**  We just have to provide the interface, because SUSv2 says:
**  "The pthread_setconcurrency() function allows an application to
**  inform the threads implementation of its desired concurrency
**  level, new_level. The actual level of concurrency provided by the
**  implementation as a result of this function call is unspecified."
*/

static int pthread_concurrency = 0;

int pthread_getconcurrency(void)
{
    return pthread_concurrency;
}

int pthread_setconcurrency(int new_level)
{
    if (new_level < 0)
        return_errno(EINVAL, EINVAL);
    pthread_concurrency = new_level;
    return OK;
}

/*
**  CONTEXT ROUTINES
*/

int pthread_key_create(pthread_key_t *key, void (*destructor)(void *))
{
    pthread_initialize();
    if (!thread_key_create((thread_key_t *)key, destructor))
        return errno;
    return OK;
}

int pthread_key_delete(pthread_key_t key)
{
    if (!thread_key_delete((thread_key_t)key))
        return errno;
    return OK;
}

int pthread_setspecific(pthread_key_t key, const void *value)
{
    if (!thread_key_setdata((thread_key_t)key, value))
        return errno;
    return OK;
}

void *pthread_getspecific(pthread_key_t key)
{
    return thread_key_getdata((thread_key_t)key);
}

/*
**  CANCEL ROUTINES
*/

int pthread_cancel(pthread_t thread)
{
    (void) thread;
  //    if (!thread_cancel((thread_t)thread))
  //      return errno;
  notimplemented(pthread_cancel);
    return OK;
}

void pthread_testcancel(void)
{
  //    thread_cancel_point();
  notimplemented(pthread_testcancel);
    return;
}

int pthread_setcancelstate(int state, int *oldstate)
{
    (void) state;
    (void) oldstate;
  /*
    int s, os;

    if (oldstate != NULL) {
        thread_cancel_state(0, &os);
        if (os & THREAD_CANCEL_ENABLE)
            *oldstate = PTHREAD_CANCEL_ENABLE;
        else
            *oldstate = PTHREAD_CANCEL_DISABLE;
    }
    if (state != 0) {
        thread_cancel_state(0, &s);
        if (state == PTHREAD_CANCEL_ENABLE) {
            s |= THREAD_CANCEL_ENABLE;
            s &= ~(THREAD_CANCEL_DISABLE);
        }
        else {
            s |= THREAD_CANCEL_DISABLE;
            s &= ~(THREAD_CANCEL_ENABLE);
        }
        thread_cancel_state(s, NULL);
    }
  */
  notimplemented(pthread_setcancelstate);
    return OK;
}

int pthread_setcanceltype(int type, int *oldtype)
{
    (void) type;
    (void) oldtype;
  /*
    int t, ot;

    if (oldtype != NULL) {
        thread_cancel_state(0, &ot);
        if (ot & THREAD_CANCEL_DEFERRED)
            *oldtype = PTHREAD_CANCEL_DEFERRED;
        else
            *oldtype = PTHREAD_CANCEL_ASYNCHRONOUS;
    }
    if (type != 0) {
        thread_cancel_state(0, &t);
        if (type == PTHREAD_CANCEL_DEFERRED) {
            t |= THREAD_CANCEL_DEFERRED;
            t &= ~(THREAD_CANCEL_ASYNCHRONOUS);
        }
        else {
            t |= THREAD_CANCEL_ASYNCHRONOUS;
            t &= ~(THREAD_CANCEL_DEFERRED);
        }
        thread_cancel_state(t, NULL);
    }
  */
  notimplemented(pthread_setcanceltype);
    return OK;
}

/*
**  SCHEDULER ROUTINES
*/

int pthread_setschedparam(pthread_t pthread, int policy, const struct sched_param *param)
{
    (void) pthread;
    (void) policy;
    (void) param;
    /* not supported */
    return_errno(ENOSYS, ENOSYS);
}

int pthread_getschedparam(pthread_t pthread, int *policy, struct sched_param *param)
{
    (void) pthread;
    (void) policy;
    (void) param;
    /* not supported */
    return_errno(ENOSYS, ENOSYS);
}

/*
**  CLEANUP ROUTINES
*/

void pthread_cleanup_push(void (*routine)(void *), void *arg)
{
    (void) routine;
    (void) arg;
    pthread_initialize();
    //    thread_cleanup_push(routine, arg);
    notimplemented(pthread_cleanup_push);
    return;
}

void pthread_cleanup_pop(int execute)
{
    (void) execute;
  //    thread_cleanup_pop(execute);
  notimplemented(pthread_cleanup_pop);
    return;
}

/*
**  AT-FORK SUPPORT
*/
int pthread_atfork(void (*prepare)(void), void (*parent)(void), void (*child)(void))
{
    (void) prepare;
    (void) parent;
    (void) child;
    return_errno(ENOSYS, ENOSYS);
}

/*
**  MUTEX ATTRIBUTE ROUTINES
*/

int pthread_mutexattr_init(pthread_mutexattr_t *attr)
{
    pthread_initialize();
    if (attr == NULL)
        return_errno(EINVAL, EINVAL);
    /* nothing to do for us */
    return OK;
}

int pthread_mutexattr_destroy(pthread_mutexattr_t *attr)
{
    if (attr == NULL)
        return_errno(EINVAL, EINVAL);
    /* nothing to do for us */
    return OK;
}

int pthread_mutexattr_setprioceiling(pthread_mutexattr_t *attr, int prioceiling)
{
    (void) prioceiling;
    if (attr == NULL)
        return_errno(EINVAL, EINVAL);
    /* not supported */
    return_errno(ENOSYS, ENOSYS);
}

int pthread_mutexattr_getprioceiling(pthread_mutexattr_t *attr, int *prioceiling)
{
    (void) prioceiling;
    if (attr == NULL)
        return_errno(EINVAL, EINVAL);
    /* not supported */
    return_errno(ENOSYS, ENOSYS);
}

int pthread_mutexattr_setprotocol(pthread_mutexattr_t *attr, int protocol)
{
    (void) protocol;
    if (attr == NULL)
        return_errno(EINVAL, EINVAL);
    /* not supported */
    return_errno(ENOSYS, ENOSYS);
}

int pthread_mutexattr_getprotocol(pthread_mutexattr_t *attr, int *protocol)
{
    (void) protocol;
    if (attr == NULL)
        return_errno(EINVAL, EINVAL);
    /* not supported */
    return_errno(ENOSYS, ENOSYS);
}

int pthread_mutexattr_setpshared(pthread_mutexattr_t *attr, int pshared)
{
    (void) pshared;
    if (attr == NULL)
        return_errno(EINVAL, EINVAL);
    /* not supported */
    return_errno(ENOSYS, ENOSYS);
}

int pthread_mutexattr_getpshared(pthread_mutexattr_t *attr, int *pshared)
{
    (void) pshared;
    if (attr == NULL)
        return_errno(EINVAL, EINVAL);
    /* not supported */
    return_errno(ENOSYS, ENOSYS);
}

int pthread_mutexattr_settype(pthread_mutexattr_t *attr, int type)
{
    (void) type;
    if (attr == NULL)
        return_errno(EINVAL, EINVAL);
    /* not supported */
    return_errno(ENOSYS, ENOSYS);
}

int pthread_mutexattr_gettype(pthread_mutexattr_t *attr, int *type)
{
    (void) type;
    if (attr == NULL)
        return_errno(EINVAL, EINVAL);
    /* not supported */
    return_errno(ENOSYS, ENOSYS);
}

/*
**  MUTEX ROUTINES
*/

int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr)
{
    mutex_t *m;
    (void) attr;

    pthread_initialize();
    if (mutex == NULL)
        return_errno(EINVAL, EINVAL);
    if ((m = (mutex_t *)malloc(sizeof(mutex_t))) == NULL)
        return errno;
    if (!thread_mutex_init(m, "pthread_mutex"))
        return errno;
    (*mutex) = (pthread_mutex_t)m;
    return OK;
}

int pthread_mutex_destroy(pthread_mutex_t *mutex)
{
    if (mutex == NULL)
        return_errno(EINVAL, EINVAL);
    free(*mutex);
    *mutex = NULL;
    return OK;
}

int pthread_mutex_setprioceiling(pthread_mutex_t *mutex, int prioceiling, int *old_ceiling)
{
    (void) prioceiling;
    (void) old_ceiling;
    if (mutex == NULL)
        return_errno(EINVAL, EINVAL);
    if (*mutex == PTHREAD_MUTEX_INITIALIZER)
        if (pthread_mutex_init(mutex, NULL) != OK)
            return errno;
    /* not supported */
    return_errno(ENOSYS, ENOSYS);
}

int pthread_mutex_getprioceiling(pthread_mutex_t *mutex, int *prioceiling)
{
    (void) prioceiling;
    if (mutex == NULL)
        return_errno(EINVAL, EINVAL);
    if (*mutex == PTHREAD_MUTEX_INITIALIZER)
        if (pthread_mutex_init(mutex, NULL) != OK)
            return errno;
    /* not supported */
    return_errno(ENOSYS, ENOSYS);
}

int pthread_mutex_lock(pthread_mutex_t *mutex)
{
    if (mutex == NULL)
        return_errno(EINVAL, EINVAL);
    if (*mutex == PTHREAD_MUTEX_INITIALIZER)
        if (pthread_mutex_init(mutex, NULL) != OK)
            return errno;
    if (!thread_mutex_lock((mutex_t *)(*mutex)))
        return errno;
    return OK;
}

int pthread_mutex_trylock(pthread_mutex_t *mutex)
{
    if (mutex == NULL)
        return_errno(EINVAL, EINVAL);
    if (*mutex == PTHREAD_MUTEX_INITIALIZER)
        if (pthread_mutex_init(mutex, NULL) != OK)
            return errno;
    if (!thread_mutex_trylock((mutex_t *)(*mutex)))
        return errno;
    return OK;
}

int pthread_mutex_unlock(pthread_mutex_t *mutex)
{
    if (mutex == NULL)
        return_errno(EINVAL, EINVAL);
    if (*mutex == PTHREAD_MUTEX_INITIALIZER)
        if (pthread_mutex_init(mutex, NULL) != OK)
            return errno;
    if (!thread_mutex_unlock((mutex_t *)(*mutex)))
        return errno;
    return OK;
}

/*
**  LOCK ATTRIBUTE ROUTINES
*/

int pthread_rwlockattr_init(pthread_rwlockattr_t *attr)
{
    pthread_initialize();
    if (attr == NULL)
        return_errno(EINVAL, EINVAL);
    /* nothing to do for us */
    return OK;
}

int pthread_rwlockattr_destroy(pthread_rwlockattr_t *attr)
{
    if (attr == NULL)
        return_errno(EINVAL, EINVAL);
    /* nothing to do for us */
    return OK;
}

int pthread_rwlockattr_setpshared(pthread_rwlockattr_t *attr, int pshared)
{
    (void) pshared;
    if (attr == NULL)
        return_errno(EINVAL, EINVAL);
    /* not supported */
    return_errno(ENOSYS, ENOSYS);
}

int pthread_rwlockattr_getpshared(const pthread_rwlockattr_t *attr, int *pshared)
{
    (void) pshared;
    if (attr == NULL)
        return_errno(EINVAL, EINVAL);
    /* not supported */
    return_errno(ENOSYS, ENOSYS);
}

/*
**  LOCK ROUTINES
*/

int pthread_rwlock_init(pthread_rwlock_t *rwlock, const pthread_rwlockattr_t *attr)
{
    rwlock_t *rw;
    (void) attr;

    pthread_initialize();
    if (rwlock == NULL)
        return_errno(EINVAL, EINVAL);
    if ((rw = (rwlock_t *)malloc(sizeof(rwlock_t))) == NULL)
        return errno;
    if (!thread_rwlock_init(rw))
        return errno;
    (*rwlock) = (pthread_rwlock_t)rw;
    return OK;
}

int pthread_rwlock_destroy(pthread_rwlock_t *rwlock)
{
    if (rwlock == NULL)
        return_errno(EINVAL, EINVAL);
    free(*rwlock);
    *rwlock = NULL;
    return OK;
}

int pthread_rwlock_rdlock(pthread_rwlock_t *rwlock)
{
    if (rwlock == NULL)
        return_errno(EINVAL, EINVAL);
    if (*rwlock == PTHREAD_RWLOCK_INITIALIZER)
        if (pthread_rwlock_init(rwlock, NULL) != OK)
            return errno;
    if (!thread_rwlock_lock((rwlock_t *)(*rwlock), RWLOCK_RD))
        return errno;
    return OK;
}

int pthread_rwlock_tryrdlock(pthread_rwlock_t *rwlock)
{
    if (rwlock == NULL)
        return_errno(EINVAL, EINVAL);
    if (*rwlock == PTHREAD_RWLOCK_INITIALIZER)
        if (pthread_rwlock_init(rwlock, NULL) != OK)
            return errno;
    if (!thread_rwlock_trylock((rwlock_t *)(*rwlock), RWLOCK_RD))
        return errno;
    return OK;
}

int pthread_rwlock_wrlock(pthread_rwlock_t *rwlock)
{
    if (rwlock == NULL)
        return_errno(EINVAL, EINVAL);
    if (*rwlock == PTHREAD_RWLOCK_INITIALIZER)
        if (pthread_rwlock_init(rwlock, NULL) != OK)
            return errno;
    if (!thread_rwlock_lock((rwlock_t *)(*rwlock), RWLOCK_RW))
        return errno;
    return OK;
}

int pthread_rwlock_trywrlock(pthread_rwlock_t *rwlock)
{
    if (rwlock == NULL)
        return_errno(EINVAL, EINVAL);
    if (*rwlock == PTHREAD_RWLOCK_INITIALIZER)
        if (pthread_rwlock_init(rwlock, NULL) != OK)
            return errno;
    if (!thread_rwlock_trylock((rwlock_t *)(*rwlock), RWLOCK_RW))
        return errno;
    return OK;
}

int pthread_rwlock_unlock(pthread_rwlock_t *rwlock)
{
    if (rwlock == NULL)
        return_errno(EINVAL, EINVAL);
    if (*rwlock == PTHREAD_RWLOCK_INITIALIZER)
        if (pthread_rwlock_init(rwlock, NULL) != OK)
            return errno;
    if (!thread_rwlock_unlock((rwlock_t *)(*rwlock)))
        return errno;
    return OK;
}

/*
**  CONDITION ATTRIBUTE ROUTINES
*/

int pthread_condattr_init(pthread_condattr_t *attr)
{
    pthread_initialize();
    if (attr == NULL)
        return_errno(EINVAL, EINVAL);
    /* nothing to do for us */
    return OK;
}

int pthread_condattr_destroy(pthread_condattr_t *attr)
{
    if (attr == NULL)
        return_errno(EINVAL, EINVAL);
    /* nothing to do for us */
    return OK;
}

int pthread_condattr_setpshared(pthread_condattr_t *attr, int pshared)
{
    (void) pshared;
    if (attr == NULL)
        return_errno(EINVAL, EINVAL);
    /* not supported */
    return_errno(ENOSYS, ENOSYS);
}

int pthread_condattr_getpshared(pthread_condattr_t *attr, int *pshared)
{
    (void) pshared;
    if (attr == NULL)
        return_errno(EINVAL, EINVAL);
    /* not supported */
    return_errno(ENOSYS, ENOSYS);
}

/*
**  CONDITION ROUTINES
*/

int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr)
{
    cond_t *cn;
    (void) attr;

    pthread_initialize();
    if (cond == NULL)
        return_errno(EINVAL, EINVAL);
    if ((cn = (cond_t *)malloc(sizeof(cond_t))) == NULL)
        return errno;
    if (!thread_cond_init(cn))
        return errno;
    (*cond) = (pthread_cond_t)cn;
    return OK;
}

int pthread_cond_destroy(pthread_cond_t *cond)
{
    if (cond == NULL)
        return_errno(EINVAL, EINVAL);
    free(*cond);
    *cond = NULL;
    return OK;
}

int pthread_cond_broadcast(pthread_cond_t *cond)
{
    if (cond == NULL)
        return_errno(EINVAL, EINVAL);
    if (*cond == PTHREAD_COND_INITIALIZER)
        if (pthread_cond_init(cond, NULL) != OK)
            return errno;
    if (!thread_cond_broadcast((cond_t *)(*cond)))
        return errno;
    return OK;
}

int pthread_cond_signal(pthread_cond_t *cond)
{
    if (cond == NULL)
        return_errno(EINVAL, EINVAL);
    if (*cond == PTHREAD_COND_INITIALIZER)
        if (pthread_cond_init(cond, NULL) != OK)
            return errno;
    if (!thread_cond_signal((cond_t *)(*cond)))
        return errno;
    return OK;
}

int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)
{
    if (cond == NULL || mutex == NULL)
        return_errno(EINVAL, EINVAL);
    if (*cond == PTHREAD_COND_INITIALIZER)
        if (pthread_cond_init(cond, NULL) != OK)
            return errno;
    if (*mutex == PTHREAD_MUTEX_INITIALIZER)
        if (pthread_mutex_init(mutex, NULL) != OK)
            return errno;
    if (!thread_cond_wait((cond_t *)(*cond), (mutex_t *)(*mutex)))
        return errno;
    return OK;
}

int pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                           const struct timespec *abstime)
{
    if (cond == NULL || mutex == NULL)
        return_errno(EINVAL, EINVAL);
    if (*cond == PTHREAD_COND_INITIALIZER)
        if (pthread_cond_init(cond, NULL) != OK)
            return errno;
    if (*mutex == PTHREAD_MUTEX_INITIALIZER)
        if (pthread_mutex_init(mutex, NULL) != OK)
            return errno;
    if (!thread_cond_timedwait((cond_t *)(*cond), (mutex_t *)(*mutex), abstime))
      return errno;
    return OK;
}

/*
**  POSIX 1003.1j
*/

/*
int pthread_abort(pthread_t thread)
{
    if (!thread_abort((thread_t)thread))
        return errno;
    return OK;
}


*/

//////////////////////////////////////////////////
// Set the emacs indentation offset
// Local Variables: ***
// c-basic-offset:4 ***
// End: ***
//////////////////////////////////////////////////
