/* libc-internal interface for mutex locks.  NPTL version.
   Copyright (C) 1996-2003, 2005, 2007 Free Software Foundation, Inc.
   This file is part of the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public License as
   published by the Free Software Foundation; either version 2.1 of the
   License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; see the file COPYING.LIB.  If not,
   write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

#ifndef _BITS_LIBC_LOCK_H
#define _BITS_LIBC_LOCK_H 1

#define __need_NULL
#include <stddef.h>

#ifdef _LIBC
# include <lowlevellock.h>
# include <tls.h>
#endif

/* Lock types.  */
typedef int __libc_lock_t; 
#define _LIBC_LOCK_INITIALIZER LLL_LOCK_INITIALIZER

typedef struct __libc_lock_recursive { 
	__libc_lock_t lock; 
	int count; 
	int owner; 
} __libc_lock_recursive_t;
#define _LIBC_LOCK_RECURSIVE_INITIALIZER {_LIBC_LOCK_INITIALIZER,0,0}

/* Define a lock variable NAME with storage class CLASS.  The lock must be
   initialized with __libc_lock_init before it can be used (or define it
   with __libc_lock_define_initialized, below).  Use `extern' for CLASS to
   declare a lock defined in another module.  In public structure
   definitions you must use a pointer to the lock structure (i.e., NAME
   begins with a `*'), because its storage size will not be known outside
   of libc.  */
#define __libc_lock_define(CLASS,NAME)\
	CLASS __libc_lock_t NAME;
#define __libc_lock_define_recursive(CLASS,NAME)\
	CLASS __libc_lock_recursive_t NAME;

/* Define an initialized lock variable NAME with storage class CLASS.
   For the C library we take a deeper look at the initializer.  For
   this implementation all fields are initialized to zero.  Therefore
   we don't initialize the variable which allows putting it into the
   BSS section.  (Except on PA-RISC and other odd architectures, where
   initialized locks must be set to one due to the lack of normal
   atomic operations.) */

#if LLL_LOCK_INITIALIZER == 0
#define __libc_lock_define_initialized(CLASS,NAME)\
	CLASS __libc_lock_t NAME;
#else
#define __libc_lock_define_initialized(CLASS,NAME)\
	CLASS __libc_lock_t NAME = _LIBC_LOCK_INITIALIZER;
#endif

/* Define an initialized recursive lock variable NAME with storage
   class CLASS.  */
#if LLL_LOCK_INITIALIZER == 0
#define __libc_lock_define_initialized_recursive(CLASS,NAME)\
	CLASS __libc_lock_recursive_t NAME;
#else
#define __libc_lock_define_initialized_recursive(CLASS,NAME)\
	CLASS __libc_lock_recursive_t NAME = _LIBC_LOCK_RECURSIVE_INITIALIZER;
#endif

/* Initialize the named lock variable, leaving it in a consistent, unlocked
   state.  */
#define __libc_lock_init(NAME) ((NAME) = _LIBC_LOCK_INITIALIZER, 0)

/* Same as last but this time we initialize a recursive mutex.  */
#if defined _LIBC && (!defined NOT_IN_libc)
#define __libc_lock_init_recursive(NAME)\
	((NAME) = (__libc_lock_recursive_t) _LIBC_LOCK_RECURSIVE_INITIALIZER, 0)
#else
#define __libc_lock_init_recursive(NAME)\
do {\
	NAME.lock = 0;\
	NAME.count = 0;\
	NAME.owner = 0;\
} while (0)
#endif

/* Finalize the named lock variable, which must be locked.  It cannot be
   used again until __libc_lock_init is called again on it.  This must be
   called on a lock variable before the containing storage is reused.  */
#define __libc_lock_fini(NAME) ((void) 0)

/* Finalize recursive named lock.  */
#define __libc_lock_fini_recursive(NAME) ((void) 0)

/* Lock the named lock variable.  */
# define __libc_lock_lock(NAME)\
	({ lll_lock (NAME, LLL_PRIVATE); 0; })

/* Lock the recursive named lock variable.  */
# define __libc_lock_lock_recursive(NAME)\
do {\
	void *self = THREAD_SELF;\
	if((NAME).owner != self) {\
		lll_lock ((NAME).lock, LLL_PRIVATE);\
		(NAME).owner = self;\
	}\
	++(NAME).count;\
} while (0)

/* Try to lock the named lock variable.  */
#define __libc_lock_trylock(NAME)\
	lll_trylock(NAME)

/* Try to lock the recursive named lock variable.  */
#define __libc_lock_trylock_recursive(NAME)\
({\
	int result = 0;\
	void *self = THREAD_SELF;\
	if((NAME).owner != self) {\
		if(lll_trylock((NAME).lock) == 0) {\
			(NAME).owner = self;\
			(NAME).count = 1;\
		}\
		else\
			result = EBUSY;\
    }\
	else\
 		++(NAME).count;\
	result;\
})

/* Unlock the named lock variable.  */
#define __libc_lock_unlock(NAME)\
	lll_unlock (NAME, LLL_PRIVATE)

/* Unlock the recursive named lock variable.  */
/* We do no error checking here.  */
# define __libc_lock_unlock_recursive(NAME)\
	do {\
		if(--(NAME).count == 0) {\
			(NAME).owner = NULL;\
			lll_unlock((NAME).lock, LLL_PRIVATE);\
		}\
	} while (0)

#define __libc_lock_default_lock_recursive(lock)\
	++((__libc_lock_recursive_t *)(lock))->count;
#define __libc_lock_default_unlock_recursive(lock)\
	--((__libc_lock_recursive_t *)(lock))->count;

/* libc's rwlocks are the same as regular locks for now... */
typedef __libc_lock_t __libc_rwlock_t; 
#define _LIBC_RWLOCK_INITIALIZER _LIBC_LOCK_INITIALIZER
typedef __libc_lock_recursive_t __libc_rwlock_recursive_t; 
#define _LIBC_RWLOCK_RECURSIVE_INITIALIZER _LIBC_LOCK_RECURSIVE_INITIALIZER
#define __libc_rwlock_define(CLASS,NAME)\
	__libc_lock_define(CLASS,NAME)
#define __libc_rwlock_define_recursive(CLASS,NAME)\
	__libc_lock_define_recursive(CLASS,NAME)
#define __libc_rwlock_define_initialized(CLASS,NAME)\
	__libc_lock_define_initialized(CLASS,NAME)
#define __libc_rwlock_define_initialized_recursive(CLASS,NAME)\
	__libc_lock_define_initialized_recursive(CLASS,NAME)
#define __libc_rwlock_init(NAME)\
	__libc_lock_init(NAME)
#define __libc_rwlock_init_recursive(NAME)\
	__libc_lock_init_recursive(NAME)
#define __libc_rwlock_fini(NAME)\
	__libc_lock_fini(NAME)
#define __libc_rwlock_fini_recursive(NAME)\
	__libc_lock_fini_recursive(NAME)
#define __libc_rwlock_rdlock(NAME)\
	__libc_lock_lock(NAME)
#define __libc_rwlock_wrlock(NAME)\
	__libc_lock_lock(NAME)
#define __libc_rwlock_rdlock_recursive(NAME)\
	__libc_lock_lock_recursive(NAME)
#define __libc_rwlock_wrlock_recursive(NAME)\
	__libc_lock_lock_recursive(NAME)
#define __libc_rwlock_tryrlock(NAME)\
	__libc_lock_trylock(NAME)
#define __libc_rwlock_trywlock(NAME)\
	__libc_lock_trylock(NAME)
#define __libc_rwlock_tryrlock_recursive(NAME)\
	__libc_lock_trylock_recursive(NAME)
#define __libc_rwlock_trywlock_recursive(NAME)\
	__libc_lock_trylock_recursive(NAME)
#define __libc_rwlock_unlock(NAME)\
	__libc_lock_unlock(NAME) 
#define __libc_rwlock_unlock_recursive(NAME)\
	__libc_lock_unlock_recursive(NAME) 
#define __libc_rwlock_default_rdlock_recursive(lock)\
	__libc_lock_default_lock_recursive(lock)
#define __libc_rwlock_default_wrlock_recursive(lock)\
	__libc_lock_default_lock_recursive(lock)
#define __libc_rwlock_default_unlock_recursive(lock)\
	__libc_lock_default_unlock_recursive(lock)

/* rtld locks are the same as libc locks */
typedef __libc_lock_t __rtld_lock_t;
#define _RTLD_LOCK_INITIALIZER _LIBC_LOCK_INITIALIZER
typedef __libc_lock_recursive_t __rtld_lock_recursive_t;
#define _RTLD_LOCK_RECURSIVE_INITIALIZER _LIBC_LOCK_RECURSIVE_INITIALIZER
#define __rtld_lock_define(CLASS,NAME)\
	__libc_lock_define_recursive(CLASS,NAME)
#define __rtld_lock_define_recursive(CLASS,NAME)\
	__libc_lock_define_recursive(CLASS,NAME)
#define __rtld_lock_define_initialized(CLASS,NAME)\
	__libc_lock_define_initialized_recursive(CLASS,NAME)
#define __rtld_lock_define_initialized_recursive(CLASS,NAME)\
	__libc_lock_define_initialized_recursive(CLASS,NAME)
#define __rtld_lock_initialize(NAME)\
	__libc_lock_init_recursive(NAME)
#define __rtld_lock_init_recursive(NAME)\
	__libc_lock_init_recursive(NAME)
# define __rtld_lock_fini(NAME)\
	__libc_lock_fini_recursive(NAME)
# define __rtld_lock_fini_recursive(NAME)\
	__libc_lock_fini_recursive(NAME)
#define __rtld_lock_lock(NAME)\
	__libc_lock_lock_recursive(NAME)
#define __rtld_lock_lock_recursive(NAME)\
	__libc_lock_lock_recursive(NAME)
#define __rtld_lock_trylock(NAME)\
	__libc_lock_trylock_recursive(NAME)
#define __rtld_lock_trylock_recursive(NAME)\
	__libc_lock_trylock_recursive(NAME)
#define __rtld_lock_unlock(NAME)\
	__libc_lock_unlock_recursive(NAME) 
#define __rtld_lock_unlock_recursive(NAME)\
	__libc_lock_unlock_recursive(NAME) 
#define __rtld_lock_default_lock_recursive(lock)\
	__libc_lock_default_lock_recursive(lock)
#define __rtld_lock_default_unlock_recursive(lock)\
	__libc_lock_default_unlock_recursive(lock)
#define __rtld_rwlock_define(CLASS,NAME)\
	__libc_rwlock_define_recursive(CLASS,NAME)
#define __rtld_rwlock_define_recursive(CLASS,NAME)\
	__libc_rwlock_define_recursive(CLASS,NAME)
#define __rtld_rwlock_define_initialized(CLASS,NAME)\
	__libc_rwlock_define_initialized_recursive(CLASS,NAME)
#define __rtld_rwlock_define_initialized_recursive(CLASS,NAME)\
	__libc_rwlock_define_initialized_recursive(CLASS,NAME)
#define __rtld_rwlock_init(NAME)\
	__libc_rwlock_init_recursive(NAME)
#define __rtld_rwlock_init_recursive(NAME)\
	__libc_rwlock_init_recursive(NAME)
#define __rtld_rwlock_fini(NAME)\
	__libc_rwlock_fini_recursive(NAME)
#define __rtld_rwlock_fini_recursive(NAME)\
	__libc_rwlock_fini_recursive(NAME)
#define __rtld_rwlock_rdlock(NAME)\
	__libc_rwlock_lock_recursive(NAME)
#define __rtld_rwlock_wrlock(NAME)\
	__libc_rwlock_lock_recursive(NAME)
#define __rtld_rwlock_rdlock_recursive(NAME)\
	__libc_rwlock_lock_recursive(NAME)
#define __rtld_rwlock_wrlock_recursive(NAME)\
	__libc_rwlock_lock_recursive(NAME)
#define __rtld_rwlock_tryrlock(NAME)\
	__libc_rwlock_trylock_recursive(NAME)
#define __rtld_rwlock_trywlock(NAME)\
	__libc_rwlock_trylock_recursive(NAME)
#define __rtld_rwlock_tryrlock_recursive(NAME)\
	__libc_rwlock_trylock_recursive(NAME)
#define __rtld_rwlock_trywlock_recursive(NAME)\
	__libc_rwlock_trylock_recursive(NAME)
#define __rtld_rwlock_unlock(NAME)\
	__libc_rwlock_unlock_recursive(NAME) 
#define __rtld_rwlock_unlock_recursive(NAME)\
	__libc_rwlock_unlock_recursive(NAME) 
#define __rtld_rwlock_default_rdlock_recursive(lock)\
	__libc_rwlock_default_lock_recursive(lock)
#define __rtld_rwlock_default_wrlock_recursive(lock)\
	__libc_rwlock_default_lock_recursive(lock)
#define __rtld_rwlock_default_unlock_recursive(lock)\
	__libc_rwlock_default_unlock_recursive(lock)

/* Define once control variable.  */
#define __libc_once_define(CLASS, NAME) CLASS int NAME = 0

/* Call handler iff the first call.  */
#define __libc_once(ONCE_CONTROL, INIT_FUNCTION)\
do {\
	if((ONCE_CONTROL) == 0) {\
		INIT_FUNCTION ();\
		(ONCE_CONTROL) = 1;\
	}\
} while (0)

/* Start a critical region with a cleanup function */
#define __libc_cleanup_region_start(DOIT, FCT, ARG)\
{\
  typeof (***(FCT)) *__save_FCT = (DOIT) ? (FCT) : 0;\
  typeof (ARG) __save_ARG = ARG;\
  /* close brace is in __libc_cleanup_region_end below. */

/* End a critical region started with __libc_cleanup_region_start. */
#define __libc_cleanup_region_end(DOIT)\
if((DOIT) && __save_FCT != 0)\
    (*__save_FCT)(__save_ARG);\
}

/* Sometimes we have to exit the block in the middle.  */
#define __libc_cleanup_end(DOIT)\
if ((DOIT) && __save_FCT != 0)\
	(*__save_FCT)(__save_ARG);\

#define __libc_cleanup_push(fct, arg) __libc_cleanup_region_start (1, fct, arg)
#define __libc_cleanup_pop(execute) __libc_cleanup_region_end (execute)

/* We need portable names for some of the functions.  */
#define __libc_mutex_unlock

/* Type for key of thread specific data.  */
typedef int __libc_key_t;

/* Create key for thread specific data.  */
#define __libc_key_create(KEY,DEST) -1

/* Set thread-specific data associated with KEY to VAL.  */
#define __libc_setspecific(KEY,VAL) ((void)0)

/* Get thread-specific data associated with KEY.  */
#define __libc_getspecific(KEY) 0

#endif	/* bits/libc-lock.h */
