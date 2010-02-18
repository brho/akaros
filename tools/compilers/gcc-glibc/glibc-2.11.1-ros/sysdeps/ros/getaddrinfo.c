/* Stub version of getaddrinfo function.
   Copyright (C) 1996, 2002 Free Software Foundation, Inc.
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
#include <netdb.h>
#include <resolv/resolv.h>
#include <arpa/inet.h>

int __nss_not_use_nscd_passwd = 1;
int __nss_not_use_nscd_group = 1;
int __nss_not_use_nscd_hosts = 1;
int __nss_not_use_nscd_services = 1;

__thread int h_errno = 0;
__thread struct __res_state *__libc_resp = NULL;

int
__res_maybe_init (res_state resp, int preinit)
{
  __set_errno (ENOSYS);
  return -1;
}
stub_warning(__res_maybe_init)
libc_hidden_def (__res_maybe_init)

int
getaddrinfo (const char *name, const char *service, const struct addrinfo *req,
	     struct addrinfo **pai)
{
  __set_errno (ENOSYS);
  return EAI_SYSTEM;
}
stub_warning (getaddrinfo)
libc_hidden_def (getaddrinfo)

void
freeaddrinfo (struct addrinfo *ai)
{
  /* Nothing.  */
}
stub_warning (freeaddrinfo)
libc_hidden_def (freeaddrinfo)

int
inet_pton(int af, const char *src, void *dst)
{
  __set_errno (ENOSYS);
  return NULL;
}
stub_warning(inet_pton)
libc_hidden_def (inet_pton)

int
__inet_aton(const char *cp, struct in_addr *addr)
{
  __set_errno (ENOSYS);
  return NULL;
}
stub_warning(__inet_aton)
weak_alias (__inet_aton, inet_aton)
libc_hidden_def (__inet_aton)
libc_hidden_weak (inet_aton)

#include <stub-tag.h>
