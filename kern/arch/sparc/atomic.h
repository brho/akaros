#ifndef ROS_INCLUDE_ATOMIC_H
#define ROS_INCLUDE_ATOMIC_H

#include <ros/common.h>
#include <ros/arch/membar.h>

typedef struct
{
	volatile uint32_t rlock;
} spinlock_t;

/* This needs to be declared over here so that comp_and_swap down below can use
 * it.  Remove this when RAMP has a proper atomic compare and swap.  (TODO) */
static inline void
(SLOCK(0) spin_lock_irqsave)(spinlock_t RACY*SAFE lock);
static inline void
(SUNLOCK(0) spin_unlock_irqsave)(spinlock_t RACY*SAFE lock);
static inline bool spin_lock_irq_enabled(spinlock_t *SAFE lock);

#define SPINLOCK_INITIALIZER {0}

// atomic_t is void*, so we can't accidentally dereference it
typedef void* atomic_t;

static inline void atomic_init(atomic_t* number, int32_t val);
static inline int32_t atomic_read(atomic_t* number);
static inline void atomic_set(atomic_t* number, int32_t val);
static inline void atomic_add(atomic_t* number, int32_t inc);
static inline void atomic_inc(atomic_t* number);
static inline void atomic_dec(atomic_t* number);
static inline long atomic_fetch_and_add(atomic_t *number, long val);
static inline bool atomic_add_not_zero(atomic_t *number, long val);
static inline bool atomic_sub_and_test(atomic_t *number, long val);
static inline void atomic_or(atomic_t *number, int mask);
static inline uint32_t atomic_swap(uint32_t* addr, uint32_t val);
static inline bool atomic_comp_swap(uint32_t *addr, uint32_t exp_val,
                                    uint32_t new_val);
static inline uint32_t spin_trylock(spinlock_t*SAFE lock);
static inline uint32_t spin_locked(spinlock_t*SAFE lock);
static inline void spin_lock(spinlock_t*SAFE lock);
static inline void spin_unlock(spinlock_t*SAFE lock);

/* Inlined functions declared above */

static inline void atomic_init(atomic_t* number, int32_t val)
{
	val <<= 8;
	__asm__ __volatile__ ("st %0,[%1]" : : "r"(val), "r"(number) : "memory");
}

static inline int32_t atomic_read(atomic_t* number)
{
	int32_t val;
	__asm__ __volatile__ ("ld [%1],%0" : "=r"(val) : "r"(number));
	return val >> 8;
}

static inline void atomic_add(atomic_t* number, int32_t inc)
{
	atomic_fetch_and_add(number, inc);
}

static inline void atomic_set(atomic_t* number, int32_t val)
{
	// this works basically the same as atomic_add... but without the add
	spin_lock((spinlock_t*)number);
	atomic_init(number,val);
}

static inline void atomic_inc(atomic_t* number)
{
	atomic_add(number,1);
}

static inline void atomic_dec(atomic_t* number)
{
	atomic_add(number,-1);
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

static inline void atomic_or(atomic_t *number, int mask)
{
	int val;
	/* this is pretty clever.  the lower 8 bits (i.e byte 3)
	 * of the atomic_t serve as a spinlock.  let's acquire it. */
	spin_lock((spinlock_t*)number);
	val = atomic_read(number);
	/* compute new counter value. */
	val |= mask;
	/* set the new counter value.  the lock is cleared (for free) */
	atomic_init(number, val);
}

static inline uint32_t atomic_swap(uint32_t* addr, uint32_t val)
{
	__asm__ __volatile__ ("swap [%2],%0" : "=r"(val) : "0"(val),"r"(addr) : "memory");
	return val;
}

// TODO: make this better! (no global locks, etc)
static inline bool atomic_comp_swap(uint32_t *addr, uint32_t exp_val,
                                    uint32_t new_val)
{
	bool retval = 0;
	uint32_t temp;
	static spinlock_t cas_lock = SPINLOCK_INITIALIZER;

	if (*addr != exp_val)
		return 0;
	spin_lock_irqsave(&cas_lock);
	if (*addr == exp_val) {
		atomic_swap(addr, new_val);
		retval = 1;
	}
	spin_unlock_irqsave(&cas_lock);
	return retval;
}

static inline uint32_t spin_trylock(spinlock_t*SAFE lock)
{
	uint32_t reg;
	__asm__ __volatile__ ("ldstub [%1+3],%0" : "=r"(reg) : "r"(&lock->rlock) : "memory");
	return reg;
}

static inline uint32_t spin_locked(spinlock_t*SAFE lock)
{
	uint32_t reg;
	__asm__ __volatile__ ("ldub [%1+3],%0" : "=r"(reg) : "r"(&lock->rlock));
	return reg;
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

static inline void spinlock_init(spinlock_t* lock)
{
	lock->rlock = 0;
}

static inline void spinlock_debug(spinlock_t* lock)
{
}

#endif /* !ROS_INCLUDE_ATOMIC_H */
