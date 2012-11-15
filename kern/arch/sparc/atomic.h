#ifndef ROS_KERN_ARCH_ATOMIC_H
#define ROS_KERN_ARCH_ATOMIC_H

#include <ros/common.h>
#include <ros/arch/membar.h>

/* This needs to be declared over here so that comp_and_swap down below can use
 * it.  Remove this when RAMP has a proper atomic compare and swap.  (TODO) */
static inline void spin_lock_irqsave(spinlock_t *lock);
static inline void spin_unlock_irqsave(spinlock_t *lock);
/* Same deal with spin locks.  Note that Sparc can deadlock on an atomic op
 * called from interrupt context (TODO) */
static inline void spin_lock(spinlock_t *lock);

/* Actual functions */
static inline void atomic_init(atomic_t *number, long val)
{
	val <<= 8;
	__asm__ __volatile__ ("st %0,[%1]" : : "r"(val), "r"(number) : "memory");
}

static inline long atomic_read(atomic_t *number)
{
	long val;
	__asm__ __volatile__ ("ld [%1],%0" : "=r"(val) : "r"(number));
	return val >> 8;
}

/* Adds val to number, returning number's original value */
static inline long atomic_fetch_and_add(atomic_t *number, long val)
{
	long retval;
	/* this is pretty clever.  the lower 8 bits (i.e byte 3)
	 * of the atomic_t serve as a spinlock.  let's acquire it. */
	spin_lock((spinlock_t*)number);
	retval = atomic_read(number);
	/* compute new counter value. */
	val += retval;
	/* set the new counter value.  the lock is cleared (for free) */
	atomic_init(number, val);
	return retval;
}

static inline void atomic_add(atomic_t *number, long val)
{
	atomic_fetch_and_add(number, val);
}

static inline void atomic_set(atomic_t *number, long val)
{
	// this works basically the same as atomic_add... but without the add
	spin_lock((spinlock_t*)number);
	atomic_init(number,val);
}

static inline void atomic_inc(atomic_t *number)
{
	atomic_add(number,1);
}

static inline void atomic_dec(atomic_t *number)
{
	atomic_add(number,-1);
}

/* Adds val to number, so long as number was not zero.  Returns TRUE if the
 * operation succeeded (added, not zero), returns FALSE if number is zero. */
static inline bool atomic_add_not_zero(atomic_t *number, long val)
{
	long num;
	bool retval = FALSE;
	/* this is pretty clever.  the lower 8 bits (i.e byte 3)
	 * of the atomic_t serve as a spinlock.  let's acquire it. */
	spin_lock((spinlock_t*)number);
	num = atomic_read(number);
	if (num) {
		num += val;
		retval = TRUE;
	}
	/* set the new (maybe old) counter value.  the lock is cleared (for free) */
	atomic_init(number, num);
	return retval;
}

/* Subtraces val from number, returning True if the new value is 0. */
static inline bool atomic_sub_and_test(atomic_t *number, long val)
{
	long num;
	bool retval = FALSE;
	/* this is pretty clever.  the lower 8 bits (i.e byte 3)
	 * of the atomic_t serve as a spinlock.  let's acquire it. */
	spin_lock((spinlock_t*)number);
	num = atomic_read(number);
	num -= val;
	retval = num ? FALSE : TRUE;
	/* set the new counter value.  the lock is cleared (for free) */
	atomic_init(number, num);
	return retval;
}

static inline void atomic_and(atomic_t *number, long mask)
{
	long val;
	/* this is pretty clever.  the lower 8 bits (i.e byte 3)
	 * of the atomic_t serve as a spinlock.  let's acquire it. */
	spin_lock((spinlock_t*)number);
	val = atomic_read(number);
	/* compute new counter value. */
	val &= mask;
	/* set the new counter value.  the lock is cleared (for free) */
	atomic_init(number, val);
}

static inline void atomic_or(atomic_t *number, long mask)
{
	long val;
	/* this is pretty clever.  the lower 8 bits (i.e byte 3)
	 * of the atomic_t serve as a spinlock.  let's acquire it. */
	spin_lock((spinlock_t*)number);
	val = atomic_read(number);
	/* compute new counter value. */
	val |= mask;
	/* set the new counter value.  the lock is cleared (for free) */
	atomic_init(number, val);
}

static inline long atomic_swap(atomic_t *addr, long val)
{
	__asm__ __volatile__ ("swap [%2],%0" : "=r"(val) : "0"(val),"r"(addr) : "memory");
	return val;
}

// TODO: make this better! (no global locks, etc)
static inline bool atomic_cas(atomic_t *addr, long exp_val, long new_val)
{
	bool retval = 0;
	long temp;
	static spinlock_t cas_lock = SPINLOCK_INITIALIZER;

	if ((long)*addr != exp_val)
		return 0;
	spin_lock_irqsave(&cas_lock);
	if ((long)*addr == exp_val) {
		atomic_swap(addr, new_val);
		retval = 1;
	}
	spin_unlock_irqsave(&cas_lock);
	return retval;
}

static inline bool atomic_cas_ptr(void **addr, void *exp_val, void *new_val)
{
	return atomic_cas((atomic_t*)addr, (long)exp_val, (long)new_val);
}

static inline bool atomic_cas_u32(uint32_t *addr, uint32_t exp_val,
                                  uint32_t new_val)
{
	return atomic_cas((atomic_t*)addr, (long)exp_val, (long)new_val);
}

static inline uint32_t spin_trylock(spinlock_t*SAFE lock)
{
	uint32_t reg;
	__asm__ __volatile__ ("ldstub [%1+3],%0" : "=r"(reg) : "r"(&lock->rlock) : "memory");
	return reg;
}

static inline bool spin_locked(spinlock_t*SAFE lock)
{
	uint32_t reg;
	__asm__ __volatile__ ("ldub [%1+3],%0" : "=r"(reg) : "r"(&lock->rlock));
	return (bool)reg;
}

static inline void spin_lock(spinlock_t*SAFE lock)
{
	while(spin_trylock(lock))
		while(spin_locked(lock));
}

static inline void spin_unlock(spinlock_t*SAFE lock)
{
	wmb();
	lock->rlock = 0;
}

static inline void __spinlock_init(spinlock_t* lock)
{
	lock->rlock = 0;
}

static inline void spinlock_debug(spinlock_t* lock)
{
}

#endif /* ROS_KERN_ARCH_ATOMIC_H */
