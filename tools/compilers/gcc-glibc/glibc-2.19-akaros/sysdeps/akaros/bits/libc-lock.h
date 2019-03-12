/* libc-internal interface for mutex locks.  Stub version.
   Copyright (C) 1996-2014 Free Software Foundation, Inc.
   This file is part of the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, see
   <http://www.gnu.org/licenses/>.  */

#ifndef _BITS_LIBC_LOCK_H
#define _BITS_LIBC_LOCK_H 1

#define __need_NULL
#include <stddef.h>

#ifdef _LIBC
# include <lowlevellock.h>
# include <tls.h>
#endif

#include <parlib/uthread.h>
#include <parlib/dtls.h>

/* Define a lock variable NAME with storage class CLASS.  The lock must be
   initialized with __libc_lock_init before it can be used (or define it
   with __libc_lock_define_initialized, below).  Use `extern' for CLASS to
   declare a lock defined in another module.  In public structure
   definitions you must use a pointer to the lock structure (i.e., NAME
   begins with a `*'), because its storage size will not be known outside
   of libc.  */
#define __libc_lock_define(CLASS, NAME) \
	CLASS uth_mutex_t NAME;
#define __libc_lock_define_recursive(CLASS, NAME) \
	CLASS uth_recurse_mutex_t NAME;
#define __libc_rwlock_define(CLASS, NAME) \
	CLASS uth_rwlock_t NAME;

/* These don't seem to be used much outside our sysdep (malloc-machine.h), but
 * the RTLD one later is used. */
#define _LIBC_LOCK_INITIALIZER UTH_MUTEX_INIT
#define _LIBC_LOCK_RECURSIVE_INITIALIZER UTH_RECURSE_MUTEX_INIT
#define _LIBC_RWLOCK_INITIALIZER UTH_RWLOCK_INIT

/* Define an initialized lock variable NAME with storage class CLASS.  */
#define __libc_lock_define_initialized(CLASS, NAME) \
	CLASS uth_mutex_t NAME = UTH_MUTEX_INIT;
#define __libc_rwlock_define_initialized(CLASS, NAME) \
	CLASS uth_rwlock_t NAME = UTH_RWLOCK_INIT;

/* Define an initialized recursive lock variable NAME with storage
   class CLASS.  */
#define __libc_lock_define_initialized_recursive(CLASS, NAME) \
	CLASS uth_recurse_mutex_t NAME = UTH_RECURSE_MUTEX_INIT;

/* Initialize the named lock variable, leaving it in a consistent, unlocked
   state.  */
#define __libc_lock_init(NAME) uth_mutex_init(&(NAME))
#define __libc_rwlock_init(NAME) uth_rwlock_init(&(NAME))

/* Same as last but this time we initialize a recursive mutex.  */
#define __libc_lock_init_recursive(NAME) uth_recurse_mutex_init(&(NAME))

/* Finalize the named lock variable, which must be locked.  It cannot be
   used again until __libc_lock_init is called again on it.  This must be
   called on a lock variable before the containing storage is reused.  */
#define __libc_lock_fini(NAME) uth_mutex_destroy(&(NAME))
#define __libc_rwlock_fini(NAME) uth_rwlock_destroy(&(NAME))

/* Finalize recursive named lock.  */
#define __libc_lock_fini_recursive(NAME) uth_recurse_mutex_destroy(&(NAME))

/* Lock the named lock variable.  */
#define __libc_lock_lock(NAME) uth_mutex_lock(&(NAME))
#define __libc_rwlock_rdlock(NAME) uth_rwlock_rdlock(&(NAME))
#define __libc_rwlock_wrlock(NAME) uth_rwlock_wrlock(&(NAME))

/* Lock the recursive named lock variable.  */
#define __libc_lock_lock_recursive(NAME) uth_recurse_mutex_lock(&(NAME))

/* Try to lock the named lock variable.  */
#define __libc_lock_trylock(NAME) \
	({ uth_mutex_trylock(&(NAME)) ? 0 : EBUSY; })
#define __libc_rwlock_tryrdlock(NAME) \
	({ uth_rwlock_try_rdlock(&(NAME)) ? 0 : EBUSY; })
#define __libc_rwlock_trywrlock(NAME) \
	({ uth_rwlock_try_wrlock(&(NAME)) ? 0 : EBUSY; })

/* Try to lock the recursive named lock variable.  */
#define __libc_lock_trylock_recursive(NAME) \
	({ uth_recurse_mutex_trylock(&(NAME)) ? 0 : EBUSY; })

/* Unlock the named lock variable.  */
#define __libc_lock_unlock(NAME) uth_mutex_unlock(&(NAME))
#define __libc_rwlock_unlock(NAME) uth_rwlock_unlock(&(NAME))

/* Unlock the recursive named lock variable.  */
#define __libc_lock_unlock_recursive(NAME) uth_recurse_mutex_unlock(&(NAME))

/* RTLD locks */
/* Ideally, we'd use uthread mutexes.  That's what pthreads does.  However, this
 * code will be in ld.so, and will never actually link against parlib.  We might
 * be able to do some function pointer magic, but for the most part, we'll
 * probably need kernel support (#futex or something).  Instead of that, we can
 * build recursive locks on top of spinlocks, and deal with any problems as they
 * arise.  By not using PDR, we run the risk of deadlocking, but I can live with
 * that for now (you'd need to dlopen() from vcore context, which would probably
 * panic for some other reason). */

typedef struct {
	unsigned int lock;
	unsigned int count;
	void *owner;
} __rtld_lock_recursive_t;

#define _RTLD_LOCK_RECURSIVE_INITIALIZER { 0, 0, (void*)-1 }

static inline void *__rtld_lock_who_am_i(void)
{
	if (atomic_read(&vcpd_of(0)->flags) & VC_SCP_NOVCCTX)
		return (void*)0xf00baa;
	/* We can't use TLS related to parlib (in_vcore_context() / vcore_id()
	 * will crash.  current_uthread won't link.).  We *can* find our thread
	 * descriptor, which disambiguates any callers (including between vcore
	 * context (which probably shouldn't be in here) and uthreads, so long
	 * as uthreads have TLS - which they must if they are making glibc
	 * calls. */
	return THREAD_SELF;
}

static inline void rtld_lock_lock_initialize(__rtld_lock_recursive_t *lock)
{
	lock->lock = 0;
	lock->count = 0;
	lock->owner = (void*)-1;
}

static inline void rtld_lock_lock_recursive(__rtld_lock_recursive_t *lock)
{
	void *me = __rtld_lock_who_am_i();

	if (lock->owner == me) {
		lock->count++;
		return;
	}
	while (__sync_lock_test_and_set(&lock->lock, 1))
		cpu_relax();
	lock->count++;
	lock->owner = me;
}

static inline void rtld_lock_unlock_recursive(__rtld_lock_recursive_t *lock)
{
	lock->count--;
	if (lock->count)
		return;
	lock->owner = (void*)-1;
	wmb();
	lock->lock = 0;
}

#define __rtld_lock_define_recursive(CLASS, NAME) \
	CLASS __rtld_lock_recursive_t NAME;
#define __rtld_lock_define_initialized_recursive(CLASS, NAME) \
	CLASS __rtld_lock_recursive_t NAME = _RTLD_LOCK_RECURSIVE_INITIALIZER;

/* __rtld_lock_initialize probably should be __rtld_lock_initialize_recursive.
 * Might be a glibc bug.  These also want &(NAME), and not NAME, hence the
 * macros. */
#define __rtld_lock_initialize(NAME) rtld_lock_lock_initialize(&(NAME))
#define __rtld_lock_lock_recursive(NAME) rtld_lock_lock_recursive(&(NAME))
#define __rtld_lock_unlock_recursive(NAME) rtld_lock_unlock_recursive(&(NAME))


/* Define once control variable.  */
#define __libc_once_define(CLASS, NAME) \
	CLASS parlib_once_t NAME

/* Call handler iff the first call.  */
#define __libc_once(ONCE_CONTROL, INIT_FUNCTION) \
	parlib_run_once(&(ONCE_CONTROL), (void (*)(void*))(INIT_FUNCTION), NULL)

/* Get once control variable.  */
#define __libc_once_get(ONCE_CONTROL) \
  ((ONCE_CONTROL).ran_once == TRUE)

/* Start a critical region with a cleanup function */
#define __libc_cleanup_region_start(DOIT, FCT, ARG)			    \
{									    \
  typeof (***(FCT)) *__save_FCT = (DOIT) ? (FCT) : 0;			    \
  typeof (ARG) __save_ARG = ARG;					    \
  /* close brace is in __libc_cleanup_region_end below. */

/* End a critical region started with __libc_cleanup_region_start. */
#define __libc_cleanup_region_end(DOIT)					    \
  if ((DOIT) && __save_FCT != 0)					    \
    (*__save_FCT)(__save_ARG);						    \
}

/* Sometimes we have to exit the block in the middle.  */
#define __libc_cleanup_end(DOIT)					    \
  if ((DOIT) && __save_FCT != 0)					    \
    (*__save_FCT)(__save_ARG);						    \

#define __libc_cleanup_push(fct, arg) __libc_cleanup_region_start (1, fct, arg)
#define __libc_cleanup_pop(execute) __libc_cleanup_region_end (execute)

/* We need portable names for some of the functions.  */
#define __libc_mutex_unlock uth_mutex_unlock

/* Type for key of thread specific data.  */
typedef dtls_key_t __libc_key_t;

/* Create key for thread specific data.  */
#define __libc_key_create(KEY,DEST)	(*KEY = dtls_key_create(DEST), 0)

/* Set thread-specific data associated with KEY to VAL.  */
#define __libc_setspecific(KEY,VAL)	set_dtls(KEY, VAL)

/* Get thread-specific data associated with KEY.  */
#define __libc_getspecific(KEY) get_dtls(KEY)

#endif	/* bits/libc-lock.h */
