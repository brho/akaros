#ifndef PARLIB_ATOMIC_H
#define PARLIB_ATOMIC_H

#include <ros/common.h>

typedef void * RACY atomic_t;

static inline void atomic_init(atomic_t *number, int32_t val);
static inline int32_t atomic_read(atomic_t *number);
static inline void atomic_set(atomic_t *number, int32_t val);
static inline void atomic_inc(atomic_t *number);
static inline void atomic_dec(atomic_t *number);
static inline uint32_t atomic_swap(uint32_t *addr, uint32_t val);
static inline bool atomic_comp_swap(uint32_t *addr, uint32_t exp_val,
                                    uint32_t new_val);
static inline void atomic_andb(volatile uint8_t RACY* number, uint8_t mask);
static inline void atomic_orb(volatile uint8_t RACY* number, uint8_t mask);

/* Inlined functions declared above */
static inline void atomic_init(atomic_t *number, int32_t val)
{
	asm volatile("movl %1,%0" : "=m"(*number) : "r"(val));
}

static inline int32_t atomic_read(atomic_t *number)
{
	int32_t val;
	asm volatile("movl %1,%0" : "=r"(val) : "m"(*number));
	return val;
}

static inline void atomic_set(atomic_t *number, int32_t val)
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

static inline uint32_t atomic_swap(uint32_t *addr, uint32_t val)
{
	// this would work, but its code is bigger, and it's not like the others
	//asm volatile("xchgl %0,(%2)" : "=r"(val) : "0"(val), "r"(addr) : "memory");
	asm volatile("xchgl %0,%1" : "=r"(val), "=m"(*addr) : "0"(val), "m"(*addr));
	return val;
}

/* reusing exp_val for the bool return */
static inline bool atomic_comp_swap(uint32_t *addr, uint32_t exp_val,
                                    uint32_t new_val)
{
	asm volatile("lock cmpxchgl %4,%1; sete %%al"
	             : "=a"(exp_val), "=m"(*addr)
	             : "m"(*addr), "a"(exp_val), "r"(new_val)
	             : "cc");
	return exp_val;
}

static inline void atomic_andb(volatile uint8_t RACY*number, uint8_t mask)
{
	asm volatile("lock andb %1,%0" : "=m"(*number) : "q"(mask) : "cc");
}

static inline void atomic_orb(volatile uint8_t RACY*number, uint8_t mask)
{
	asm volatile("lock orb %1,%0" : "=m"(*number) : "q"(mask) : "cc");
}

#endif /* !PARLIB_ATOMIC_H */
