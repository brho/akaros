/* Copyright (C) 1991,1995-1997,2000,2002,2009 Free Software Foundation, Inc.
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
#include <stdint.h>
#include <unistd.h>
#include <bits/libc-lock.h>
#include <ros/syscall.h>
#include <ros/memlayout.h>
#include <ros/procdata.h>
#include <sys/mman.h>
#include <parlib/spinlock.h>

static uintptr_t curbrk = BRK_START;

/* brk() is called by malloc, which holds spinlocks.  So we need to use
 * spinlocks too.  It is possible that the kernel will block in the mmap() call,
 * in which case the process would spin.  That's already the case for malloc,
 * regardless of what we do here in brk() (since ultimately, brk() can block. */
static struct spin_pdr_lock __brk_lock = SPINPDR_INITIALIZER;

static bool is_early_scp(void)
{
	struct preempt_data *vcpd = &__procdata.vcore_preempt_data[0];

	return (uintptr_t)vcpd->flags & VC_SCP_NOVCCTX;
}

/* Early SCP context doesn't need the locks, since we're single threaded, and we
 * can't grab the PDR locks in some cases.  Specifically, we might not have a
 * TLS for thread 0 yet, so we can't do things like check in_vcore_context(). */
static void brk_lock(void)
{
	if (is_early_scp())
		return;
	spin_pdr_lock(&__brk_lock);
}

static void brk_unlock(void)
{
	if (is_early_scp())
		return;
	spin_pdr_unlock(&__brk_lock);
}

static uintptr_t
__internal_getbrk (void)
{
  return curbrk;
}

static int
__internal_setbrk (uintptr_t addr)
{
  uintptr_t real_new_brk = (addr + PGSIZE - 1)/PGSIZE*PGSIZE;
  uintptr_t real_brk = (__internal_getbrk() + PGSIZE - 1)/PGSIZE*PGSIZE;

  if(real_new_brk > real_brk)
  {
    if(real_new_brk > BRK_END)
      return -1;
	// calling mmap directly to avoid referencing errno before it is
	// initialized.
    if ((void*)__ros_syscall_noerrno(SYS_mmap, (long)real_brk,
				     real_new_brk-real_brk, PROT_READ |
				     PROT_WRITE | PROT_EXEC, MAP_FIXED |
				     MAP_ANONYMOUS | MAP_PRIVATE, -1, 0)
	!= (void*)real_brk)
      return -1;
  }
  else if(real_new_brk < real_brk)
  {
    if (real_new_brk < BRK_START)
      return -1;

    if (munmap((void*)real_new_brk, real_brk - real_new_brk))
      return -1;
  }

  curbrk = addr;
  return 0;
}

/* Set the end of the process's data space to ADDR.
   Return 0 if successful, -1 if not.   */
int
__brk (void* addr)
{
  if(addr == 0)
    return 0;

  brk_lock();
  int ret = __internal_setbrk((uintptr_t)addr);
  brk_unlock();

  return ret;
}
weak_alias (__brk, brk)

/* Extend the process's data space by INCREMENT.
   If INCREMENT is negative, shrink data space by - INCREMENT.
   Return start of new space allocated, or -1 for errors.  */
void *
__sbrk (intptr_t increment)
{
  brk_lock();

  uintptr_t oldbrk = __internal_getbrk();
  if ((increment > 0
       ? (oldbrk + (uintptr_t) increment < oldbrk)
       : (oldbrk < (uintptr_t) -increment))
      || __internal_setbrk (oldbrk + increment) < 0)
    oldbrk = -1;

  brk_unlock();

  return (void*)oldbrk;
}
libc_hidden_def (__sbrk)
weak_alias (__sbrk, sbrk)
