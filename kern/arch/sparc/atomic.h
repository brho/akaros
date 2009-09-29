#ifndef ROS_INCLUDE_ATOMIC_H
#define ROS_INCLUDE_ATOMIC_H

#include <ros/common.h>

#define mb() {rmb(); wmb();}
#define rmb()
#define wmb() ({ asm volatile("stbar"); })
/* Force a wmb, used in cases where an IPI could beat a write, even though
 * write-orderings are respected.  (Used by x86) */
#define wmb_f() wmb()

typedef volatile uint32_t spinlock_t;

// atomic_t is void*, so we can't accidentally dereference it
typedef void* atomic_t;

static inline void atomic_init(atomic_t* number, int32_t val);
static inline int32_t atomic_read(atomic_t* number);
static inline void atomic_set(atomic_t* number, int32_t val);
static inline void atomic_add(atomic_t* number, int32_t inc);
static inline void atomic_inc(atomic_t* number);
static inline void atomic_dec(atomic_t* number);
static inline uint32_t spin_trylock(spinlock_t*SAFE lock);
static inline void spin_lock(spinlock_t*SAFE lock);
static inline void spin_unlock(spinlock_t*SAFE lock);

/* Inlined functions declared above */

static inline void atomic_init(atomic_t* number, int32_t val)
{
	val <<= 8;
	asm volatile ("st %0,[%1]" : : "r"(val), "r"(number) : "memory");
}

static inline int32_t atomic_read(atomic_t* number)
{
	int32_t val;
	asm volatile ("ld [%1],%0" : "=r"(val) : "r"(number));
	return val >> 8;
}

static inline void atomic_add(atomic_t* number, int32_t inc)
{
	// this is pretty clever.  the lower 8 bits (i.e byte 3)
	// of the atomic_t serve as a spinlock.  let's acquire it.
	{ TRUSTEDBLOCK spin_lock((spinlock_t*)number); }

	// compute new counter value.
	inc += atomic_read(number);

	// set the new counter value.  the lock is cleared (for free)
	atomic_init(number,inc);
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

static inline uint32_t spin_trylock(spinlock_t*SAFE lock)
{
	uint32_t reg;
	asm volatile("ldstub [%1+3],%0" : "=r"(reg) : "r"(lock) : "memory");
	return reg;
}

static inline uint32_t spin_locked(spinlock_t*SAFE lock)
{
	uint32_t reg;
	asm volatile("ldub [%1+3],%0" : "=r"(reg) : "r"(lock));
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
	asm volatile("stub %%g0,[%0+3]" : : "r"(lock) : "memory");
}

#endif /* !ROS_INCLUDE_ATOMIC_H */
