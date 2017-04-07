/* Copyright (C) 2002, 2003 Free Software Foundation, Inc.
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
   License along with the GNU C Library; if not, write to the Free
   Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307 USA.  */

#pragma once

#include <parlib/uthread.h>

__BEGIN_DECLS

/* Value returned if `sem_open' failed.  */
#define SEM_FAILED      ((sem_t *) 0)

typedef struct {
	uth_semaphore_t				real_sem;
} sem_t;

int sem_init(sem_t *__sem, int __pshared, unsigned int __value);
int sem_destroy(sem_t *__sem);
sem_t *sem_open(__const char *__name, int __oflag, ...);
int sem_close(sem_t *__sem);
int sem_unlink(__const char *__name);
int sem_wait(sem_t *__sem);
int sem_timedwait(sem_t *sem, const struct timespec *abs_timeout);
int sem_trywait(sem_t *__sem);
int sem_post(sem_t *__sem);
int sem_getvalue(sem_t *__restrict __sem, int *__restrict __sval);

__END_DECLS
