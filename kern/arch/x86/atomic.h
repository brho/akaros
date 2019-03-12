/* Copyright (c) 2009-2011 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * x86 atomics and locking functions. */

#pragma once

#include <ros/common.h>
#include <arch/membar.h>
#include <arch/x86.h>
#include <arch/arch.h>

static inline void atomic_andb(volatile uint8_t *number, uint8_t mask);
static inline void atomic_orb(volatile uint8_t *number, uint8_t mask);

/* Inlined functions declared above */
static inline void atomic_init(atomic_t *number, long val)
{
	asm volatile("mov %1,%0" : "=m"(*number) : "r"(val));
}

static inline long atomic_read(atomic_t *number)
{
	long val;
	asm volatile("mov %1,%0" : "=r"(val) : "m"(*number));
	return val;
}

static inline void atomic_set(atomic_t *number, long val)
{
	asm volatile("mov %1,%0" : "=m"(*number) : "r"(val));
}

static inline void atomic_add(atomic_t *number, long val)
{
	__sync_fetch_and_add(number, val);
}

static inline void atomic_inc(atomic_t *number)
{
	__sync_fetch_and_add(number, 1);
}

static inline void atomic_dec(atomic_t *number)
{
	__sync_fetch_and_sub(number, 1);
}

static inline long atomic_fetch_and_add(atomic_t *number, long val)
{
	return (long)__sync_fetch_and_add(number, val);
}

static inline void atomic_and(atomic_t *number, long mask)
{
	__sync_fetch_and_and(number, mask);
}

static inline void atomic_or(atomic_t *number, long mask)
{
	__sync_fetch_and_or(number, mask);
}

static inline long atomic_swap(atomic_t *addr, long val)
{
	/* This poorly named function does an xchg */
	return (long)__sync_lock_test_and_set(addr, val);
}

static inline void *atomic_swap_ptr(void **addr, void *val)
{
	return (void*)__sync_lock_test_and_set(addr, val);
}

static inline bool atomic_cas(atomic_t *addr, long exp_val, long new_val)
{
	return __sync_bool_compare_and_swap(addr, exp_val, new_val);
}

static inline bool atomic_cas_ptr(void **addr, void *exp_val, void *new_val)
{
	return __sync_bool_compare_and_swap(addr, exp_val, new_val);
}

static inline bool atomic_cas_u32(uint32_t *addr, uint32_t exp_val,
                                  uint32_t new_val)
{
	return __sync_bool_compare_and_swap(addr, exp_val, new_val);
}

/* Adds val to number, so long as number was not zero.  Returns TRUE if the
 * operation succeeded (added, not zero), returns FALSE if number is zero. */
static inline bool atomic_add_not_zero(atomic_t *number, long val)
{
	long old_num, new_num;
	do {
		old_num = atomic_read(number);
		if (!old_num)
			return FALSE;
		new_num = old_num + val;
	} while (!atomic_cas(number, old_num, new_num));
	return TRUE;
}

/* Subtracts val from number, returning True if the new value is 0. */
static inline bool atomic_sub_and_test(atomic_t *number, long val)
{
	bool b;
	asm volatile("lock sub %2,%1; setz %0" : "=q"(b), "=m"(*number)
	                                       : "r"(val), "m"(*number)
	                                       : "cc" );
	return b;
}

static inline void atomic_andb(volatile uint8_t *number, uint8_t mask)
{
	__sync_fetch_and_and(number, mask);
}

static inline void atomic_orb(volatile uint8_t *number, uint8_t mask)
{
	__sync_fetch_and_or(number, mask);
}

static inline bool spin_locked(spinlock_t *lock)
{
	// the lock status is the lowest byte of the lock
	return lock->rlock & 0xff;
}

static inline void __spin_lock_raw(volatile uint32_t *rlock)
{
	uint8_t dicks = 0;
	asm volatile("1:                      "
	             "	cmpb $0, %0;          "
	             "	je 2f;                "
	             "	pause;                "
	             "	jmp 1b;               "
	             "2:                      "
	             "	movb $1, %1;          "
	             "	xchgb %1, %0;         "
	             "	cmpb $0, %1;          "
	             "	jne 1b;               "
	             : : "m"(*rlock), "r"(dicks) : "cc");
	cmb();	/* need cmb(), the CPU mb() was handled by the xchg */
}

static inline void __spin_lock(spinlock_t *lock)
{
	__spin_lock_raw(&lock->rlock);
}

static inline bool __spin_trylock(spinlock_t *lock)
{
	/* since this is an or, we're not going to clobber the top bytes (if
	 * that matters) */
	return !__sync_fetch_and_or(&lock->rlock, 1);
}

static inline void __spin_unlock(spinlock_t *lock)
{
	/* Need to prevent the compiler from reordering older stores. */
	wmb();
	rwmb();	/* x86 makes both of these a cmb() */
	lock->rlock = 0;
}

static inline void __spinlock_init(spinlock_t *lock)
{
	lock->rlock = 0;
}
