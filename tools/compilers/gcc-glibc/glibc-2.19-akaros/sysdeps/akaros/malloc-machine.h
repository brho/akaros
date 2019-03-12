/* Basic platform-independent macro definitions for mutexes,
   thread-specific data and parameters for malloc.
   Copyright (C) 2003-2014 Free Software Foundation, Inc.
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

#ifndef _MALLOC_MACHINE_H
#define _MALLOC_MACHINE_H

#include <atomic.h>
#include <lowlevellock.h>

typedef struct spin_pdr_lock mutex_t;

/* These macros expect to take a pointer to the object */
#define mutex_init(m)		spin_pdr_init(m)
#define mutex_lock(m)		spin_pdr_lock(m)
#define mutex_trylock(m)	({ spin_pdr_trylock(m) ? 0 : EBUSY; })
#define mutex_unlock(m)		spin_pdr_unlock(m)
#define MUTEX_INITIALIZER	SPINPDR_INITIALIZER

/* thread specific data for glibc */

#include <bits/libc-tsd.h>

typedef void* tsd_key_t;	/* no key data structure, libc magic does it */
__libc_tsd_define (static, void *, MALLOC) /* declaration/common definition */
#define tsd_key_create(key, destr) ((void) (key))
#define tsd_setspecific(key, data) __libc_tsd_set (void *, MALLOC, (data))
#define tsd_getspecific(key, vptr) ((vptr) = __libc_tsd_get (void *, MALLOC))

/* TODO: look into pthread's version.  We might need this, and it could be that
 * glibc has the fork_cbs already. */
#define thread_atfork(prepare, parent, child) do {} while(0)

#include <sysdeps/generic/malloc-machine.h>

#endif /* !defined(_MALLOC_MACHINE_H) */
