#ifndef PARLIB_ARCH_ATOMIC_H
#define PARLIB_ARCH_ATOMIC_H

#include <ros/common.h>
#include <ros/atomic.h>

static inline void atomic_init(atomic_t *number, long val);
static inline long atomic_read(atomic_t *number);
static inline void atomic_set(atomic_t *number, long val);
static inline void atomic_inc(atomic_t *number);
static inline void atomic_dec(atomic_t *number);
static inline long atomic_swap(atomic_t *addr, long val);
static inline void *atomic_swap_ptr(void **addr, void *val);
static inline uint32_t atomic_swap_u32(uint32_t *addr, uint32_t val);
static inline bool atomic_cas(atomic_t *addr, long exp_val, long new_val);
static inline bool atomic_cas_ptr(void **addr, void *exp_val, void *new_val);
static inline bool atomic_cas_u32(uint32_t *addr, uint32_t exp_val,
                                  uint32_t new_val);
static inline void atomic_andb(volatile uint8_t RACY* number, uint8_t mask);
static inline void atomic_orb(volatile uint8_t RACY* number, uint8_t mask);

/* Inlined functions declared above */
static inline void atomic_init(atomic_t *number, long val)
{
	asm volatile("movl %1,%0" : "=m"(*number) : "r"(val));
}

static inline long atomic_read(atomic_t *number)
{
	int32_t val;
	asm volatile("movl %1,%0" : "=r"(val) : "m"(*number));
	return val;
}

static inline void atomic_set(atomic_t *number, long val)
{
	asm volatile("movl %1,%0" : "=m"(*number) : "r"(val));
}

// need to do this with pointers and deref.  %0 needs to be the memory address
static inline void atomic_inc(atomic_t *number)
{
	asm volatile("lock incl %0" : "=m"(*number) : : "cc");
}

static inline void atomic_dec(atomic_t *number)
{
	// for instance, this doesn't work:
	//asm volatile("lock decl (%0)" : "=r"(number) : : "cc");
	asm volatile("lock decl %0" : "=m"(*number) : : "cc");
}

static inline long atomic_swap(atomic_t *addr, long val)
{
	// this would work, but its code is bigger, and it's not like the others
	//asm volatile("xchgl %0,(%2)" : "=r"(val) : "0"(val), "r"(addr) : "memory");
	asm volatile("xchgl %0,%1" : "=r"(val), "=m"(*addr) : "0"(val), "m"(*addr));
	return val;
}

static inline void *atomic_swap_ptr(void **addr, void *val)
{
	asm volatile("xchgl %0,%1" : "=r"(val), "=m"(*addr) : "0"(val), "m"(*addr));
	return val;
}

static inline uint32_t atomic_swap_u32(uint32_t *addr, uint32_t val)
{
	asm volatile("xchgl %0,%1" : "=r"(val), "=m"(*addr) : "0"(val), "m"(*addr));
	return val;
}

/* reusing exp_val for the bool return.  1 (TRUE) for success (like test).  Need
 * to zero eax, since it will get set if the cmpxchgl failed. */
static inline bool atomic_cas(atomic_t *addr, long exp_val, long new_val)
{
	asm volatile("lock cmpxchgl %4,%1; movl $0,%%eax; sete %%al"
	             : "=a"(exp_val), "=m"(*addr)
	             : "m"(*addr), "a"(exp_val), "r"(new_val)
	             : "cc", "memory");
	return exp_val;
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

static inline void atomic_andb(volatile uint8_t RACY*number, uint8_t mask)
{
	asm volatile("lock andb %1,%0" : "=m"(*number) : "q"(mask) : "cc");
}

static inline void atomic_orb(volatile uint8_t RACY*number, uint8_t mask)
{
	asm volatile("lock orb %1,%0" : "=m"(*number) : "q"(mask) : "cc");
}

#endif /* !PARLIB_ARCH_ATOMIC_H */
