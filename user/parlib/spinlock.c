/* Copyright (c) 2013 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * Kevin Klues <klueska@cs.berkeley.edu>
 *
 * See LICENSE for details. */

#include <stdlib.h>
#include <errno.h>
#include <parlib/assert.h>

#include <parlib/spinlock.h>
#include <parlib/vcore.h>
#include <parlib/uthread.h>

void spinlock_init(spinlock_t *lock)
{
	lock->locked = FALSE;
}

/* Returns TRUE if we grabbed the lock */
bool spinlock_trylock(spinlock_t *lock)
{
	if (lock->locked)
		return FALSE;
	return !__sync_lock_test_and_set(&lock->locked, TRUE);
}

void spinlock_lock(spinlock_t *lock) 
{
	while (!spinlock_trylock(lock))
		cpu_relax();
}

void spinlock_unlock(spinlock_t *lock) 
{
	__sync_lock_release(&lock->locked, FALSE);
}

bool spinlock_locked(spinlock_t *lock)
{
	return lock->locked;
}

/* Spin-PRD locks (preemption detection/recovery).  Idea is to CAS and put the
 * lockholder's vcoreid in the lock, and all spinners ensure that vcore runs. */
void spin_pdr_init(struct spin_pdr_lock *pdr_lock)
{
	/* See glibc-2.19-akaros/sysdeps/akaros/lowlevellock.h for details. */
	parlib_static_assert(sizeof(struct spin_pdr_lock) == sizeof(int));
	pdr_lock->lock = SPINPDR_UNLOCKED;
}

/* Internal version of the locking func, doesn't care if notifs are disabled */
void __spin_pdr_lock(struct spin_pdr_lock *pdr_lock)
{
	uint32_t vcoreid = vcore_id();
	uint32_t lock_val;
	do {
		while ((lock_val = pdr_lock->lock) != SPINPDR_UNLOCKED) {
			ensure_vcore_runs(lock_val);
			cmb();
		}
	} while (!atomic_cas_u32(&pdr_lock->lock, lock_val, vcoreid));
	cmb();	/* just need a cmb, the CAS handles the CPU wmb/wrmb() */
}

void __spin_pdr_unlock(struct spin_pdr_lock *pdr_lock)
{
	/* could make an arch-dependent 'release barrier' out of these */
	wmb();	/* Need to prevent the compiler from reordering older stores. */
	rwmb();	/* No old reads passing either.   x86 makes both mbs a cmb() */
	pdr_lock->lock = SPINPDR_UNLOCKED;
}

bool spin_pdr_locked(struct spin_pdr_lock *pdr_lock)
{
	return pdr_lock->lock != SPINPDR_UNLOCKED;
}

void spin_pdr_lock(struct spin_pdr_lock *pdr_lock)
{
	/* Disable notifs, if we're an _M uthread */
	uth_disable_notifs();
	__spin_pdr_lock(pdr_lock);
}

void spin_pdr_unlock(struct spin_pdr_lock *pdr_lock)
{
	__spin_pdr_unlock(pdr_lock);
	/* Enable notifs, if we're an _M uthread */
	uth_enable_notifs();
}

bool spin_pdr_trylock(struct spin_pdr_lock *pdr_lock)
{
	uint32_t lock_val;

	uth_disable_notifs();
	lock_val = ACCESS_ONCE(pdr_lock->lock);
	if (lock_val != SPINPDR_UNLOCKED) {
		uth_enable_notifs();
		return FALSE;
	}
	if (atomic_cas_u32(&pdr_lock->lock, lock_val, vcore_id())) {
		return TRUE;
	} else {
		uth_enable_notifs();
		return FALSE;
	}
}
