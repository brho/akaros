/* Definition of `errno' variable.  Canonical version.
   Copyright (C) 2002, 2004 Free Software Foundation, Inc.
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

#include <errno.h>
#include <tls.h>
#include <dl-sysdep.h>
#undef errno

#if RTLD_PRIVATE_ERRNO

/* Code compiled for rtld has errno #defined to rtld_errno. */
int rtld_errno attribute_hidden;
#define errno rtld_errno
char __errstr_tls[MAX_ERRSTR_LEN] = {0};

#else

__thread int errno;
extern __thread int __libc_errno __attribute__ ((alias ("errno")))
  attribute_hidden;
__thread char __errstr_tls[MAX_ERRSTR_LEN] = {0};

#endif

/* this is the glibc default, exported so uthread.c can use them */
int *__errno_location_tls(void)
{
	return &errno;
}

char *__errstr_location_tls(void)
{
	return __errstr_tls;
}

/* These func ptrs will be used to access errno_loc and errstr_loc.  can be
 * overriden at runtime (uthread code will do this) */
int *(*ros_errno_loc)(void) = __errno_location_tls;
char *(*ros_errstr_loc)(void) = __errstr_location_tls;

/* errno calls this (and derefs the result).  it's also called internally */
int *__errno_location(void)
{
	return ros_errno_loc();
}

/* libc doesn't really know about errstr, but we'll use it in syscall.c */
char *errstr(void)
{
	return ros_errstr_loc();
}

/* Don't try to hidden_data_def the function pointers.  Won't allow us to
 * switch, or otherwise track the right location. */
libc_hidden_def(__errno_location_tls)
libc_hidden_def(__errstr_location_tls)
libc_hidden_def(__errno_location)
libc_hidden_def(errstr)
