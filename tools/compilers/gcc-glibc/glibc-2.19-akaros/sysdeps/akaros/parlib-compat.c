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
#include <parlib/uthread.h>
#include <parlib/dtls.h>
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
 * functions that ld.so calls.
 *
 * Also, be careful and paranoid.  If you change or add functions in here,
 * recompile apps that link against libc - even if they link dynamically.
 * Otherwise, when they linked with libc.so, *libc itself* (not the actual
 * program) would not find the parlib functions - it would still use these
 * functions.  I don't have a good explanation for it, but that's what seemed to
 * happen.
 *
 * For an example, if you write(2, "foo\n", 4) on every lock acquisition, you'll
 * see one foo per process, which I think comes from ld (back when it used
 * spin_pdr locks for the rtld lock).  Later functions that call spin_pdr_lock,
 * whether from the app, parlib, or libc, do not output foo.  This is not the
 * case if the application was not rebuilt before this change (e.g. bash, ssh,
 * etc). */

__thread int __weak_vcoreid = 0;
extern __thread int __vcoreid __attribute__ ((weak, alias ("__weak_vcoreid")));

__thread bool __weak_vcore_context = FALSE;
extern __thread bool __vcore_context
       __attribute__ ((weak, alias ("__weak_vcore_context")));

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
	breakpoint();
	abort();
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

void __spin_pdr_init(struct spin_pdr_lock *pdr_lock)
{
	assert(0);
}
weak_alias(__spin_pdr_init, spin_pdr_init)

bool __spin_pdr_trylock(struct spin_pdr_lock *pdr_lock)
{
	assert(0);
}
weak_alias(__spin_pdr_trylock, spin_pdr_trylock)

void __spin_pdr_lock(struct spin_pdr_lock *pdr_lock)
{
	assert(0);
}
weak_alias(__spin_pdr_lock, spin_pdr_lock)

void __spin_pdr_unlock(struct spin_pdr_lock *pdr_lock)
{
	assert(0);
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

void __uth_mutex_init(uth_mutex_t *m)
{
	assert(0);
}
weak_alias(__uth_mutex_init, uth_mutex_init)

void __uth_mutex_destroy(uth_mutex_t *m)
{
	assert(0);
}
weak_alias(__uth_mutex_destroy, uth_mutex_destroy)

void __uth_mutex_lock(uth_mutex_t *m)
{
	assert(0);
}
weak_alias(__uth_mutex_lock, uth_mutex_lock)

bool __uth_mutex_trylock(uth_mutex_t *m)
{
	assert(0);
}
weak_alias(__uth_mutex_trylock, uth_mutex_trylock)

void __uth_mutex_unlock(uth_mutex_t *m)
{
	assert(0);
}
weak_alias(__uth_mutex_unlock, uth_mutex_unlock)

void __uth_recurse_mutex_init(uth_recurse_mutex_t *r_m)
{
	assert(0);
}
weak_alias(__uth_recurse_mutex_init, uth_recurse_mutex_init)

void __uth_recurse_mutex_destroy(uth_recurse_mutex_t *r_m)
{
	assert(0);
}
weak_alias(__uth_recurse_mutex_destroy, uth_recurse_mutex_destroy)

void __uth_recurse_mutex_lock(uth_recurse_mutex_t *r_m)
{
	assert(0);
}
weak_alias(__uth_recurse_mutex_lock, uth_recurse_mutex_lock)

bool __uth_recurse_mutex_trylock(uth_recurse_mutex_t *r_m)
{
	assert(0);
}
weak_alias(__uth_recurse_mutex_trylock, uth_recurse_mutex_trylock)

void __uth_recurse_mutex_unlock(uth_recurse_mutex_t *r_m)
{
	assert(0);
}
weak_alias(__uth_recurse_mutex_unlock, uth_recurse_mutex_unlock)

void __uth_rwlock_init(uth_rwlock_t *rwl)
{
	assert(0);
}
weak_alias(__uth_rwlock_init, uth_rwlock_init)

void __uth_rwlock_destroy(uth_rwlock_t *rwl)
{
	assert(0);
}
weak_alias(__uth_rwlock_destroy, uth_rwlock_destroy)

void __uth_rwlock_rdlock(uth_rwlock_t *rwl)
{
	assert(0);
}
weak_alias(__uth_rwlock_rdlock, uth_rwlock_rdlock)

bool __uth_rwlock_try_rdlock(uth_rwlock_t *rwl)
{
	assert(0);
}
weak_alias(__uth_rwlock_try_rdlock, uth_rwlock_try_rdlock)

void __uth_rwlock_wrlock(uth_rwlock_t *rwl)
{
	assert(0);
}
weak_alias(__uth_rwlock_wrlock, uth_rwlock_wrlock)

bool __uth_rwlock_try_wrlock(uth_rwlock_t *rwl)
{
	assert(0);
}
weak_alias(__uth_rwlock_try_wrlock, uth_rwlock_try_wrlock)

void __uth_rwlock_unlock(uth_rwlock_t *rwl)
{
	assert(0);
}
weak_alias(__uth_rwlock_unlock, uth_rwlock_unlock)

dtls_key_t __dtls_key_create(dtls_dtor_t dtor)
{
	assert(0);
}
weak_alias(__dtls_key_create, dtls_key_create)

void __set_dtls(dtls_key_t key, const void *dtls)
{
	assert(0);
}
weak_alias(__set_dtls, set_dtls)

void *__get_dtls(dtls_key_t key)
{
	assert(0);
}
weak_alias(__get_dtls, get_dtls)
