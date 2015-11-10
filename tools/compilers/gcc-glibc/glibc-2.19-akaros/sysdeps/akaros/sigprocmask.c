/* Copyright (C) 1991-2014 Free Software Foundation, Inc.
 * This file is part of the GNU C Library.
 *
 * The GNU C Library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * The GNU C Library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the GNU C Library; if not, see
 * <http://www.gnu.org/licenses/>.
 */

#include <errno.h>
#include <parlib/signal.h>


/* If SET is not NULL, modify the current set of blocked signals
 * according to HOW, which may be SIG_BLOCK, SIG_UNBLOCK or SIG_SETMASK.
 * If OSET is not NULL, store the old set of blocked signals in *OSET.  */
int __sigprocmask(int how, const sigset_t *set, sigset_t *oset)
{
	return signal_ops->sigprocmask(how, set, oset);
}
weak_alias(__sigprocmask, sigprocmask)
