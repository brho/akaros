#ifndef ROS_INCLUDE_ATOMIC_H
#define ROS_INCLUDE_ATOMIC_H

#include <ros/common.h>
#include <arch/arch.h>

typedef void* atomic_t;
struct spinlock {
	volatile uint32_t rlock;
};
typedef struct spinlock spinlock_t;
#define SPINLOCK_INITIALIZER {0}

bool atomic_comp_swap(uintptr_t* addr, uintptr_t exp_val, uintptr_t new_val);

static inline void atomic_init(atomic_t* number, uintptr_t val)
{
  *(uintptr_t*)number = val;
}

static inline uintptr_t atomic_read(atomic_t* number)
{
  return *(uintptr_t*)number;
}

static inline void atomic_set(atomic_t* number, uintptr_t val)
{
  *(uintptr_t*)number = val;
}

/* Adds val to number, returning number's original value */
static inline uintptr_t atomic_fetch_and_add(atomic_t* number, uintptr_t val)
{
	return __sync_fetch_and_add((uintptr_t*)number, val);
}

static inline void atomic_add(atomic_t* number, uintptr_t val)
{
	atomic_fetch_and_add(number, val);
}

static inline void atomic_inc(atomic_t* number)
{
	atomic_add(number, 1);
}

static inline void atomic_dec(atomic_t* number)
{
	atomic_add(number, -1);
}

/* Adds val to number, so long as number was not zero.	Returns TRUE if the
 * operation succeeded (added, not zero), returns FALSE if number is zero. */
static inline bool atomic_add_not_zero(atomic_t* number, uintptr_t val)
{
	uintptr_t old_num, new_num;
	do {
		old_num = atomic_read(number);
		if (!old_num)
			return FALSE;
		new_num = old_num + val;
	} while (!atomic_comp_swap((uintptr_t*)number, old_num, new_num));
	return TRUE;
}

/* Subtraces val from number, returning True if the new value is 0. */
static inline bool atomic_sub_and_test(atomic_t* number, uintptr_t val)
{
	return __sync_fetch_and_sub((uintptr_t*)number, val) == val;
}

static inline void atomic_and(atomic_t *number, uintptr_t mask)
{
	__sync_fetch_and_and(number, mask);
}

static inline void atomic_or(atomic_t* number, uintptr_t mask)
{
	__sync_fetch_and_or(number, mask);
}

static inline uintptr_t atomic_swap(uintptr_t* addr, uintptr_t val)
{
	return __sync_lock_test_and_set(addr, val); // yes, really
}

// RISC-V has atomic word ops, not byte ops, so we must manipulate addresses
static inline void atomic_andb(volatile uint8_t* number, uint8_t mask)
{
	uintptr_t offset = (uintptr_t)number & 3;
	uint32_t wmask = (1<<(8*offset+8)) - (1<<(8*offset));
	wmask = ~wmask | ((uint32_t)mask << (8*offset));

	__sync_fetch_and_and((uint32_t*)((uintptr_t)number & ~3), wmask);
}

static inline void atomic_orb(volatile uint8_t* number, uint8_t mask)
{
	uintptr_t offset = (uintptr_t)number & 3;
	uint32_t wmask = (uint32_t)mask << (8*offset);

	__sync_fetch_and_or((uint32_t*)((uintptr_t)number & ~3), wmask);
}

static inline uint32_t spin_locked(spinlock_t* lock)
{
	return lock->rlock;
}

static inline uint32_t spin_trylock(spinlock_t* lock)
{
	return __sync_fetch_and_or(&lock->rlock, 1);
}

static inline void spin_lock(spinlock_t *lock)
{
	while(spin_trylock(lock))
		while(lock->rlock);
}

static inline void spin_unlock(spinlock_t *lock)
{
	/* Need to prevent the compiler (and some arches) from reordering older
	 * stores */
	wmb();
	lock->rlock = 0;
}

static inline void spinlock_init(spinlock_t *lock)
{
	lock->rlock = 0;
}

static inline void spinlock_debug(spinlock_t* lock)
{
}

#endif /* !ROS_INCLUDE_ATOMIC_H */
