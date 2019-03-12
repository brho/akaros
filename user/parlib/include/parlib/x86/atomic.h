#pragma once

#include <ros/atomic.h>

__BEGIN_DECLS

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

static inline void atomic_inc(atomic_t *number)
{
	(void)__sync_fetch_and_add(number, 1);
}

static inline void atomic_dec(atomic_t *number)
{
	(void)__sync_fetch_and_sub(number, 1);
}

static inline long atomic_fetch_and_add(atomic_t *number, long val)
{
	return (long)__sync_fetch_and_add(number, val);
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

static inline uint32_t atomic_swap_u32(uint32_t *addr, uint32_t val)
{
	return (uint32_t)__sync_lock_test_and_set(addr, val);
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

static inline void atomic_andb(volatile uint8_t *number, uint8_t mask)
{
	(void)__sync_fetch_and_and(number, mask);
}

static inline void atomic_orb(volatile uint8_t *number, uint8_t mask)
{
	(void)__sync_fetch_and_or(number, mask);
}

__END_DECLS
