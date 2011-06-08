#ifndef PARLIB_ARCH_ATOMIC_H
#define PARLIB_ARCH_ATOMIC_H

/* Unlike in x86, we need to include spinlocks in the user atomic ops file.
 * Since compare and swap isn't truely non-blocking, and we can't disable
 * interrupts in userspace, there is a slight chance of deadlock. */

#include <ros/common.h>
#include <ros/atomic.h>
#include <ros/arch/membar.h>

typedef struct
{
	volatile uint32_t rlock;
} spinlock_t;

#define SPINLOCK_INITIALIZER {0}

static inline void atomic_init(atomic_t *number, long val);
static inline long atomic_read(atomic_t *number);
static inline void atomic_set(atomic_t *number, long val);
static inline void atomic_inc(atomic_t *number);
static inline void atomic_dec(atomic_t *number);
static inline long atomic_fetch_and_add(atomic_t *number, long val);
static inline long atomic_swap(atomic_t *addr, long val);
static inline void *atomic_swap_ptr(void **addr, void *val);
static inline uint32_t atomic_swap_u32(uint32_t *addr, uint32_t val);
static inline bool atomic_cas(atomic_t *addr, long exp_val, long new_val);
static inline bool atomic_cas_ptr(void **addr, void *exp_val, void *new_val);
static inline bool atomic_cas_u32(uint32_t *addr, uint32_t exp_val,
                                  uint32_t new_val);
static inline void atomic_or_int(volatile int *number, int mask);
static inline uint32_t spin_trylock(spinlock_t*SAFE lock);
static inline uint32_t spin_locked(spinlock_t*SAFE lock);
static inline void spin_lock(spinlock_t*SAFE lock);
static inline void spin_unlock(spinlock_t*SAFE lock);

/* Inlined functions declared above */

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

/* Sparc needs atomic add, but the regular ROS atomic add conflicts with
 * glibc's internal one. */
static inline void ros_atomic_add(atomic_t *number, long inc)
{
	atomic_fetch_and_add(number, inc);
}

static inline void atomic_set(atomic_t *number, long val)
{
	// this works basically the same as atomic_add... but without the add
	spin_lock((spinlock_t*)number);
	atomic_init(number,val);
}

static inline void atomic_inc(atomic_t *number)
{
	ros_atomic_add(number,1);
}

static inline void atomic_dec(atomic_t *number)
{
	ros_atomic_add(number,-1);
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

static inline long atomic_swap(atomic_t *addr, long val)
{
	__asm__ __volatile__ ("swap [%2],%0" : "=r"(val) : "0"(val),"r"(addr) : "memory");
	return val;
}

static inline void *atomic_swap_ptr(void **addr, void *val)
{
	__asm__ __volatile__ ("swap [%2],%0" : "=r"(val) : "0"(val),"r"(addr) : "memory");
	return val;
}

static inline uint32_t atomic_swap_u32(uint32_t *addr, uint32_t val)
{
	__asm__ __volatile__ ("swap [%2],%0" : "=r"(val) : "0"(val),"r"(addr) : "memory");
	return val;
}

static inline bool atomic_cas(atomic_t *addr, long exp_val, long new_val)
{
	bool retval = 0;
	long temp;
	static spinlock_t cas_lock = SPINLOCK_INITIALIZER;

	if ((long)*addr != exp_val)
		return 0;
	spin_lock(&cas_lock);
	if ((long)*addr == exp_val) {
		atomic_swap(addr, new_val);
		retval = 1;
	}
	spin_unlock(&cas_lock);
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

static inline void atomic_or_int(volatile int *number, int mask)
{
	int val;
	/* this is pretty clever.  the lower 8 bits (i.e byte 3)
	 * of the atomic_t serve as a spinlock.  let's acquire it. */
	spin_lock((spinlock_t*)number);
	val = atomic_read((atomic_t*)number);
	/* compute new counter value. */
	val |= mask;
	/* set the new counter value.  the lock is cleared (for free) */
	atomic_init((atomic_t*)number, val);
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

#endif /* !PARLIB_ARCH_ATOMIC_H */
