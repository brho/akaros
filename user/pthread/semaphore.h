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

#ifndef _SEMAPHORE_H
#define _SEMAPHORE_H	1

/* Get the definition for sem_t.  */
#define __SIZEOF_SEM_T	16

/* Value returned if `sem_open' failed.  */
#define SEM_FAILED      ((sem_t *) 0)

typedef union
{
  char __size[__SIZEOF_SEM_T];
  long int __attribute__ ((aligned (__BIGGEST_ALIGNMENT__)));
} sem_t;

extern int sem_init (sem_t *__sem, int __pshared, unsigned int __value);
extern int sem_destroy (sem_t *__sem);
extern sem_t *sem_open (__const char *__name, int __oflag, ...);
extern int sem_close (sem_t *__sem);
extern int sem_unlink (__const char *__name);
extern int sem_wait (sem_t *__sem);
extern int sem_trywait (sem_t *__sem);
extern int sem_post (sem_t *__sem);
extern int sem_getvalue (sem_t *__restrict __sem, int *__restrict __sval);

#endif	/* semaphore.h */
