/* Copyright (c) 2015 Google Inc.
 * Kevin Klues <klueska@cs.berkeley.edu>
 * See LICENSE for details.
 */

#include <libc-symbols.h>
#include <ros/common.h>
#include <ros/trapframe.h>
#include <ros/syscall.h>
#include <parlib/stdio.h>
#include <parlib/assert.h>
#include <parlib/spinlock.h>
#include <parlib/timing.h>
#include <stdbool.h>

/* Here we define functions and variables that are really defined in parlib, but
 * we need them in libc in order to link it. We weak alias them here so that the
 * parlib definitions will override them later.
 *
 * Unfortunately, this trick only works so long as we leave parlib as a static
 * library. If we ever decide to make parlib a .so, then we will have to revisit
 * this and use function pointers at runtime or something similar.
 *
 * This also doesn't work for ld.so, which doesn't link against parlib.  That's
 * probably a good thing (uthread constructors would be a mess for ld, I bet).
 * But that does mean that these stubs need to actually do something for
 * functions that ld.so calls.  See the notes below for more info. */

__thread int __weak_vcoreid = 0;
weak_alias(__weak_vcoreid, __vcoreid);

__thread bool __weak_vcore_context = FALSE;
weak_alias(__weak_vcore_context, __vcore_context);

int __akaros_printf(const char *format, ...)
{
	assert(0);
	return -1;
}
weak_alias(__akaros_printf, akaros_printf)

void __print_user_context(struct user_context *ctx)
{
	assert(0);
}
weak_alias(__print_user_context, print_user_context)

void ___assert_failed(const char *file, int line, const char *msg)
{
	assert(0);
}
weak_alias(___assert_failed, _assert_failed)

uint64_t __nsec2tsc(uint64_t nsec)
{
	assert(0);
}
weak_alias(__nsec2tsc, nsec2tsc)

uint64_t __tsc2nsec(uint64_t tsc_time)
{
	assert(0);
}
weak_alias(__tsc2nsec, tsc2nsec)

/* ld.so calls these, so we need them to work.  We don't need them to be
 * thread-safe, since we're single-threaded, but we do need them to use the
 * right values for 'locked' and 'unlocked'.
 *
 * Note that for this change I needed to recompile the binaries that link with
 * libc - even if they link dynamically.  Otherwise, when they linked with
 * libc.so, *libc itself* (not the actual program) would not find the parlib
 * functions - it would still use these functions.  Lots of glibc functions
 * (printf, fflush, etc) call some version of spin_pdr_lock (lll_lock).
 *
 * For an example, if you write(2, "foo\n", 4) on ever lock acquisition, you'll
 * see one foo per process, which I think comes from ld.  Later functions that
 * call spin_pdr_lock, whether from the app, parlib, or libc, do not output foo.
 * This is not the case if the application was not rebuilt before this change
 * (e.g. bash, ssh, etc). */
bool __spin_pdr_trylock(struct spin_pdr_lock *pdr_lock)
{
	if (pdr_lock->lock != SPINPDR_UNLOCKED)
		return FALSE;
	pdr_lock->lock = 0;
	return TRUE;
}
weak_alias(__spin_pdr_trylock, spin_pdr_trylock)

void __spin_pdr_lock(struct spin_pdr_lock *pdr_lock)
{
	assert(pdr_lock->lock == SPINPDR_UNLOCKED);
	/* assume we're vcore 0 */
	pdr_lock->lock = 0;
}
weak_alias(__spin_pdr_lock, spin_pdr_lock)

void __spin_pdr_unlock(struct spin_pdr_lock *pdr_lock)
{
	pdr_lock->lock = SPINPDR_UNLOCKED;
}
weak_alias(__spin_pdr_unlock, spin_pdr_unlock)

void __cpu_relax_vc(uint32_t vcoreid)
{
	cpu_relax();
}
weak_alias(__cpu_relax_vc, cpu_relax_vc)

void __uthread_sched_yield(void)
{
	/* In the off-chance we're called before parlib is available, we'll do the
	 * single-threaded, SCP yield. */
	ros_syscall(SYS_proc_yield, TRUE, 0, 0, 0, 0, 0);
}
weak_alias(__uthread_sched_yield, uthread_sched_yield)
