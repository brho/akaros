#ifndef GCC_GTHR_AKAROS_H
#define GCC_GTHR_AKAROS_H

/* Akaros threads specific definitions.
 *
 * See gthr.h for more info. */

#define __GTHREADS 1
#define __GTHREAD_HAS_COND 1
#define __GTHREADS_CXX0X 1

#include <parlib/uthread.h>
#include <parlib/dtls.h>
#include <ros/errno.h>

typedef dtls_key_t __gthread_key_t;
typedef parlib_once_t __gthread_once_t;
#define __GTHREAD_ONCE_INIT PARLIB_ONCE_INIT
typedef uth_mutex_t __gthread_mutex_t;
typedef uth_recurse_mutex_t __gthread_recursive_mutex_t;
typedef uth_cond_var_t __gthread_cond_t;
typedef struct uthread * __gthread_t;
typedef struct timespec __gthread_time_t;

/* It'd be nice to be able to use these static initializers, and they work in
 * C, but thanks to C++ not being able to do things that C can
 * (https://gcc.gnu.org/bugzilla/show_bug.cgi?id=55606), these give us errors.
 * We should be OK if we use the INIT functions.  Incidentally, I think you
 * need the INIT functions even if you have the macros defined, contrary to the
 * guidance. */
//#define __GTHREAD_MUTEX_INIT UTH_MUTEX_INIT
//#define __GTHREAD_RECURSIVE_MUTEX_INIT UTH_RECURSE_MUTEX_INIT
//#define __GTHREAD_COND_INIT UTH_COND_VAR_INIT

static inline int __gthread_active_p(void)
{
	return uth_2ls_is_multithreaded() ? 1 : 0;
}

static inline int __gthread_once(__gthread_once_t *once, void (*func)(void))
{
	parlib_run_once(once, (void (*)(void *))func, NULL);
	return 0;
}

static inline void __gthread_mutex_init_function(__gthread_mutex_t *gth_mtx)
{
	uth_mutex_init(gth_mtx);
}
#define __GTHREAD_MUTEX_INIT_FUNCTION __gthread_mutex_init_function

static inline int __gthread_mutex_destroy(__gthread_mutex_t *gth_mtx)
{
	uth_mutex_destroy(gth_mtx);
	return 0;
}

static inline int __gthread_mutex_lock(__gthread_mutex_t *gth_mtx)
{
	uth_mutex_lock(gth_mtx);
	return 0;
}

static inline int __gthread_mutex_trylock(__gthread_mutex_t *gth_mtx)
{
	return uth_mutex_trylock(gth_mtx) ? 0 : EBUSY;
}

static inline int __gthread_mutex_unlock(__gthread_mutex_t *gth_mtx)
{
	uth_mutex_unlock(gth_mtx);
	return 0;
}

static inline void
__gthread_recursive_mutex_init_function(__gthread_recursive_mutex_t *gth_r_mtx)
{
	uth_recurse_mutex_init(gth_r_mtx);
}
#define __GTHREAD_RECURSIVE_MUTEX_INIT_FUNCTION \
        __gthread_recursive_mutex_init_function

static inline int
__gthread_recursive_mutex_destroy(__gthread_recursive_mutex_t *gth_r_mtx)
{
	uth_recurse_mutex_destroy(gth_r_mtx);
	return 0;
}

static inline int
__gthread_recursive_mutex_lock(__gthread_recursive_mutex_t *gth_r_mtx)
{
	uth_recurse_mutex_lock(gth_r_mtx);
	return 0;
}

static inline int
__gthread_recursive_mutex_trylock(__gthread_recursive_mutex_t *gth_r_mtx)
{
	return uth_recurse_mutex_trylock(gth_r_mtx) ? 0 : EBUSY;
}

static inline int
__gthread_recursive_mutex_unlock(__gthread_recursive_mutex_t *gth_r_mtx)
{
	uth_recurse_mutex_unlock(gth_r_mtx);
	return 0;
}

static inline int __gthread_key_create(__gthread_key_t *keyp,
                                       void (*dtor)(void *))
{
	*keyp = dtls_key_create(dtor);
	return 0;
}

static inline int __gthread_key_delete(__gthread_key_t key)
{
	dtls_key_delete(key);
	return 0;
}

static inline void *__gthread_getspecific(__gthread_key_t key)
{
	return get_dtls(key);
}

static inline int __gthread_setspecific(__gthread_key_t key, const void *ptr)
{
	set_dtls(key, ptr);
	return 0;
}

static inline void __gthread_cond_init_function(__gthread_cond_t *cond)
{
	uth_cond_var_init(cond);
}
#define __GTHREAD_COND_INIT_FUNCTION __gthread_cond_init_function

static inline int __gthread_cond_destroy(__gthread_cond_t *cond)
{
	uth_cond_var_destroy(cond);
	return 0;
}

static inline int __gthread_cond_broadcast(__gthread_cond_t *cond)
{
	uth_cond_var_broadcast(cond);
	return 0;
}

static inline int __gthread_cond_wait(__gthread_cond_t *cond,
                                      __gthread_mutex_t *mutex)
{
	uth_cond_var_wait(cond, mutex);
	return 0;
}

static inline int
__gthread_cond_wait_recursive(__gthread_cond_t *cond,
                              __gthread_recursive_mutex_t *mutex)
{
	uth_cond_var_wait_recurse(cond, mutex);
	return 0;
}

static inline int __gthread_create(__gthread_t *thread, void *(*func) (void*),
                                   void *args)
{
	*thread = uthread_create(func, args);
	return *thread ? 0 : -1;
}

static inline int __gthread_join(__gthread_t thread, void **value_ptr)
{
	uthread_join(thread, value_ptr);
	return 0;
}

static inline int __gthread_detach(__gthread_t thread)
{
	uthread_detach(thread);
	return 0;
}

static inline int __gthread_equal(__gthread_t t1, __gthread_t t2)
{
	return t1 == t2;
}

static inline __gthread_t __gthread_self(void)
{
	return uthread_self();
}

static inline int __gthread_yield(void)
{
	uthread_sched_yield();
	return 0;
}

static inline int __gthread_mutex_timedlock(__gthread_mutex_t *m,
                                            const __gthread_time_t *abs_timeout)
{
	return uth_mutex_timed_lock(m, abs_timeout) ? 0 : -ETIMEDOUT;
}

static inline int
__gthread_recursive_mutex_timedlock(__gthread_recursive_mutex_t *m,
                                    const __gthread_time_t *abs_timeout)
{
	return uth_recurse_mutex_timed_lock(m, abs_timeout) ? 0 : -ETIMEDOUT;
}

static inline int __gthread_cond_signal(__gthread_cond_t *cond)
{
	uth_cond_var_signal(cond);
	return 0;
}

static inline int __gthread_cond_timedwait(__gthread_cond_t *cond,
                                           __gthread_mutex_t *mutex,
                                           const __gthread_time_t *abs_timeout)
{
	return uth_cond_var_timed_wait(cond, mutex, abs_timeout) ? 0 : -ETIMEDOUT;
}

#endif /* GCC_GTHR_AKAROS_H */
