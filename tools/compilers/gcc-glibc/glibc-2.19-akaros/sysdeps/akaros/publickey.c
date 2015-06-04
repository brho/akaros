/* Get public or secret key from key server.
   Copyright (C) 1996-2014 Free Software Foundation, Inc.
   This file is part of the GNU C Library.
   Contributed by Ulrich Drepper <drepper@cygnus.com>, 1996.

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

/* AKAROS_PORT: removed references to nss; just stubbed the funcs */

#include <errno.h>
#include <rpc/netdb.h>
#include <rpc/auth_des.h>


/* Type of the lookup function for the public key.  */
typedef int (*public_function) (const char *, char *, int *);

/* Type of the lookup function for the secret key.  */
typedef int (*secret_function) (const char *, char *, const char *, int *);


int
getpublickey (const char *name, char *key)
{
  return FALSE;
}
libc_hidden_nolink_sunrpc (getpublickey, GLIBC_2_0)
stub_warning(getpublickey)


int
getsecretkey (const char *name, char *key, const char *passwd)
{
  return FALSE;
}
libc_hidden_nolink_sunrpc (getsecretkey, GLIBC_2_0)
stub_warning(getsecretkey)
