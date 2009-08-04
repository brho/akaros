#ifndef ROS_INCLUDE_ATOMIC_H
#define ROS_INCLUDE_ATOMIC_H

#include <arch/types.h>

#define mb() {rmb(); wmb();}
#define rmb()
#define wmb() ({ asm volatile("stbar"); })

typedef volatile uint32_t spinlock_t;

//linux style atomic ops
typedef struct {volatile int32_t real_num;} atomic_t;
#define atomic_read(atom) ((atom)->real_num >> 8)
#define atomic_init(i) {(i) << 8}
//and the atomic incs, etc take an atomic_t ptr, deref inside

static inline void atomic_set(atomic_t*SAFE number, int32_t val);
static inline void atomic_add(atomic_t*SAFE number, int32_t inc);
static inline void atomic_inc(atomic_t*SAFE number);
static inline void atomic_dec(atomic_t*SAFE number);
static inline uint32_t spin_trylock(spinlock_t*SAFE lock);
static inline void spin_lock(spinlock_t*SAFE lock);
static inline void spin_unlock(spinlock_t*SAFE lock);

/* Inlined functions declared above */

static inline void atomic_add(atomic_t*SAFE number, int32_t inc)
{
	// this is pretty clever.  the lower 8 bits (i.e byte 3)
	// of the atomic_t serve as a spinlock.  let's acquire it.
	spin_lock((spinlock_t*SAFE)number);

	// compute new counter value.
	// must shift the old counter right by 8
	inc += number->real_num >> 8;

	// set the new counter value.
	// since the lower 8 bits will be cleared by the shift,
	// we also release the lock (for free!)
	number->real_num = inc << 8;
}

static inline void atomic_set(atomic_t*SAFE number, uint32_t val)
{
	// this works basically the same as atomic_add
	spin_lock((spinlock_t*)number);
	number->real_num = val << 8;
}

static inline void atomic_inc(atomic_t*SAFE number)
{
	atomic_add(number,1);
}

static inline void atomic_dec(atomic_t*SAFE number)
{
	atomic_add(number,-1);
}

static inline uint32_t spin_trylock(spinlock_t*SAFE lock)
{
	// we don't need to initialize reg, but it quiets the compiler
	uint32_t reg;
	asm volatile("ldstub [%1+3],%0"
	             : "=r"(reg)
	             : "r"(lock)
	             : "memory");
	return reg;
}

static inline uint8_t spin_locked(spinlock_t*SAFE lock)
{
	return *((volatile uint8_t*COUNT(sizeof(spinlock_t)))lock+3);
}

static inline void spin_lock(spinlock_t*SAFE lock)
{
	while(spin_trylock(lock))
		while(spin_locked(lock));
}

static inline void spin_unlock(spinlock_t*SAFE lock)
{
	wmb();
 	*((volatile uint8_t*COUNT(sizeof(spinlock_t)))lock+3) = 0;
}

#endif /* !ROS_INCLUDE_ATOMIC_H */
