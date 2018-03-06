#pragma once

#include <ros/common.h>
#include <arch/arch.h>

#ifdef __riscv64
# define LR_P "lr.d"
# define SC_P "sc.d"
#else
# define LR_P "lr.w"
# define SC_P "sc.w"
#endif

static bool atomic_cas(atomic_t *addr, long exp_val, long new_val)
{
  return __sync_bool_compare_and_swap(addr, exp_val, new_val);
}

static bool atomic_cas_ptr(void** addr, void* exp_val, void* new_val)
{
  return __sync_bool_compare_and_swap(addr, exp_val, new_val);
}

static bool atomic_cas_u32(uint32_t *addr, uint32_t exp_val, uint32_t new_val)
{
  return __sync_bool_compare_and_swap(addr, exp_val, new_val);
}

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
static inline bool atomic_add_not_zero(atomic_t *num, long inc)
{
	long res, tmp;
	asm volatile ("1:\n"
	              LR_P " %0, 0(%2)\n"     // tmp = *num; lock line
	              "li    %1, 1\n"         // res = 1
	              "beqz  %0, 2f\n"        // if (val == 0) goto fail
	              "add   %0, %0, %3\n"    // tmp += inc
	              SC_P " %1, %0, 0(%2)\n" // if (locked) *num = tmp
	              "bnez  %1, 1b\n"        // else goto retry
				  "2:\n"
	              : "=&r"(tmp), "=&r"(res) : "r"(num), "r"(inc) : "memory");
	return res == 0;
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

static inline void *atomic_swap_ptr(void **addr, void *val)
{
	return (void*)__sync_lock_test_and_set(addr, val);
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

static inline bool __spin_trylock(spinlock_t *lock)
{
	return !__sync_fetch_and_or(&lock->rlock, 1);
}

static inline void __spin_lock(spinlock_t *lock)
{
	do
	{
		while (lock->rlock)
			;
	} while (!__spin_trylock(lock));
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
