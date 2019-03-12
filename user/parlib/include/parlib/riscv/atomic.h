#pragma once

#include <parlib/common.h>
#include <ros/atomic.h>
#include <ros/arch/membar.h>

__BEGIN_DECLS

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

/* Inlined functions declared above */

static inline void atomic_init(atomic_t *number, long val)
{
	*(volatile long*)number = val;
}

static inline long atomic_read(atomic_t *number)
{
	return *(volatile long*)number;
}

static inline void ros_atomic_add(atomic_t *number, long inc)
{
	atomic_fetch_and_add(number, inc);
}

static inline void atomic_set(atomic_t *number, long val)
{
	atomic_init(number, val);
}

static inline void atomic_inc(atomic_t *number)
{
	ros_atomic_add(number, 1);
}

static inline void atomic_dec(atomic_t *number)
{
	ros_atomic_add(number, -1);
}

/* Adds val to number, returning number's original value */
static inline long atomic_fetch_and_add(atomic_t *number, long val)
{
	return __sync_fetch_and_add((long*)number, val);
}

static inline long atomic_swap(atomic_t *addr, long val)
{
	return __sync_lock_test_and_set((long*)addr, val);
}

static inline void *atomic_swap_ptr(void **addr, void *val)
{
	return __sync_lock_test_and_set(addr, val);
}

static inline uint32_t atomic_swap_u32(uint32_t *addr, uint32_t val)
{
	return __sync_lock_test_and_set(addr, val);
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

static inline void atomic_or_int(volatile int *number, int mask)
{
	__sync_fetch_and_or(number, mask);
}

__END_DECLS
