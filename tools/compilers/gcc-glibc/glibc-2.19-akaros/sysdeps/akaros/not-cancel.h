/* Uncancelable versions of cancelable interfaces.
   Copyright (C) 2012-2014 Free Software Foundation, Inc.
   This file is part of the GNU C Library.
   Contributed by Chris Metcalf <cmetcalf@tilera.com>, 2012.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library.  If not, see
   <http://www.gnu.org/licenses/>.  */

#ifndef _AKAROS_GENERIC_NOT_CANCEL_H
#define _AKAROS_GENERIC_NOT_CANCEL_H

#include <sysdeps/generic/not-cancel.h>

/* Uncancelable open with openat.  */
#undef open_not_cancel
#define open_not_cancel(name, flags, mode) \
  __open (name, flags, mode)
#undef open_not_cancel_2
#define open_not_cancel_2(name, flags) \
  __open (name, flags)

#endif
