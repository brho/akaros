#ifndef ROS_KERN_ARCH_ATOMIC_H
#define ROS_KERN_ARCH_ATOMIC_H

#include <ros/common.h>
#include <arch/arch.h>

bool atomic_cas(atomic_t *addr, long exp_val, long new_val);
bool atomic_cas_ptr(void **addr, void *exp_val, void *new_val);
bool atomic_cas_u32(uint32_t *addr, uint32_t exp_val, uint32_t new_val);

static inline void atomic_init(atomic_t *number, long val)
{
  *(volatile long*)number = val;
}

static inline long atomic_read(atomic_t *number)
{
  return *(volatile long*)number;
}

static inline void atomic_set(atomic_t *number, long val)
{
  *(volatile long*)number = val;
}

/* Adds val to number, returning number's original value */
static inline long atomic_fetch_and_add(atomic_t *number, long val)
{
	return __sync_fetch_and_add((long*)number, val);
}

static inline void atomic_add(atomic_t *number, long val)
{
	atomic_fetch_and_add(number, val);
}

static inline void atomic_inc(atomic_t *number)
{
	atomic_add(number, 1);
}

static inline void atomic_dec(atomic_t *number)
{
	atomic_add(number, -1);
}

/* Adds val to number, so long as number was not zero.	Returns TRUE if the
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

/* Subtraces val from number, returning True if the new value is 0. */
static inline bool atomic_sub_and_test(atomic_t *number, long val)
{
	return __sync_fetch_and_sub((long*)number, val) == val;
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
	return (long)__sync_lock_test_and_set(addr, val); // yes, really
}

static inline uint32_t atomic_swap_u32(uint32_t *addr, uint32_t val)
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

static inline bool spin_locked(spinlock_t* lock)
{
	return lock->rlock;
}

static inline uint32_t spin_trylock(spinlock_t* lock)
{
	return __sync_fetch_and_or(&lock->rlock, 1);
}

static inline void __spin_lock(spinlock_t *lock)
{
	do
	{
		while (lock->rlock)
			;
	} while (spin_trylock(lock));
	mb();
}

static inline void __spin_unlock(spinlock_t *lock)
{
	mb();
	lock->rlock = 0;
}

static inline void __spinlock_init(spinlock_t *lock)
{
	lock->rlock = 0;
}

static inline void spinlock_debug(spinlock_t* lock)
{
}

#endif /* ROS_KERN_ARCH_ATOMIC_H */
