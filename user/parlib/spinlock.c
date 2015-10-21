/* Copyright (c) 2013 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * Kevin Klues <klueska@cs.berkeley.edu>
 *
 * Spinlocks and Spin-PDR locks (preemption detection/recovery)
 *
 * This file is part of Parlib.
 * 
 * Parlib is free software: you can redistribute it and/or modify
 * it under the terms of the Lesser GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * Parlib is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * Lesser GNU General Public License for more details.
 * 
 * See COPYING.LESSER for details on the GNU Lesser General Public License.
 * See COPYING for details on the GNU General Public License. */

#include <stdlib.h>
#include <errno.h>
#include <parlib/assert.h>

#include <parlib/spinlock.h>
#include <parlib/vcore.h>
#include <parlib/uthread.h>

void spinlock_init(spinlock_t *lock)
{
	lock->lock = 0;
}

int spinlock_trylock(spinlock_t *lock) 
{
	if (lock->lock)
		return EBUSY;
	return __sync_lock_test_and_set(&lock->lock, EBUSY);
}

void spinlock_lock(spinlock_t *lock) 
{
	while (spinlock_trylock(lock))
		cpu_relax();
}

void spinlock_unlock(spinlock_t *lock) 
{
	__sync_lock_release(&lock->lock, 0);
}

bool spinlock_locked(spinlock_t *lock)
{
	return lock->lock != 0;
}

/* Two different versions, with and without CAS.  Default is with CAS. */
#ifndef CONFIG_SPINPDR_NO_CAS /* CAS version */

/* Spin-PRD locks (preemption detection/recovery).  Idea is to CAS and put the
 * lockholder's vcoreid in the lock, and all spinners ensure that vcore runs. */
void spin_pdr_init(struct spin_pdr_lock *pdr_lock)
{
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
	rwmb();	/* And no old reads passing either.   x86 makes both mbs a cmb() */
	pdr_lock->lock = SPINPDR_UNLOCKED;
}

bool spin_pdr_locked(struct spin_pdr_lock *pdr_lock)
{
	return pdr_lock->lock != SPINPDR_UNLOCKED;
}

#else /* NON-CAS version */

/* Using regular spinlocks, with SPINPDR_VCOREID_UNKNOWN (-1) meaning 'no
 * lockholder advertised yet'.  There are two spots where the lockholder still
 * holds the lock but hasn't advertised its vcoreid, and in those cases we
 * ensure all vcores aren't preempted (linear scan). */
void spin_pdr_init(struct spin_pdr_lock *pdr_lock)
{
	spinlock_init(&pdr_lock->spinlock);
	pdr_lock->lockholder = SPINPDR_VCOREID_UNKNOWN;
}

void __spin_pdr_lock(struct spin_pdr_lock *pdr_lock)
{
	uint32_t vcoreid = vcore_id();
	uint32_t ensure_tgt;
	while (spinlock_trylock(&pdr_lock->spinlock)) {
		ensure_tgt = pdr_lock->lockholder;
		/* ensure will make sure *every* vcore runs if you pass it your self. */
		if (ensure_tgt == SPINPDR_VCOREID_UNKNOWN)
			ensure_tgt = vcoreid;
		ensure_vcore_runs(ensure_tgt);
		cpu_relax();
	}
	pdr_lock->lockholder = vcoreid;
}

void __spin_pdr_unlock(struct spin_pdr_lock *pdr_lock)
{
	pdr_lock->lockholder = SPINPDR_VCOREID_UNKNOWN;
	spinlock_unlock(&pdr_lock->spinlock);
}

bool spin_pdr_locked(struct spin_pdr_lock *pdr_lock)
{
	return spinlock_locked(&pdr_lock->spinlock);
}

#endif /* CONFIG_SPINPDR_NO_CAS */

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
