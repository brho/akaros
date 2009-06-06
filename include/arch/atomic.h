#ifndef ROS_INCLUDE_ATOMIC_H
#define ROS_INCLUDE_ATOMIC_H

#include <arch/types.h>

#define mb() {rmb(); wmb();}
#define rmb() ({ asm volatile("lfence"); })
#define wmb() 

static inline void atomic_inc(volatile uint32_t* number);
static inline void atomic_dec(volatile uint32_t* number);
static inline void atomic_andb(volatile uint8_t* number, uint8_t mask);

// need to do this with pointers and deref.  %0 needs to be the memory address
static inline void atomic_inc(volatile uint32_t* number)
{
	asm volatile("lock incl %0" : "=m"(*number) : : "cc");
}

static inline void atomic_dec(volatile uint32_t* number)
{
	asm volatile("lock decl %0" : "=m"(*number) : : "cc");
}

static inline void atomic_andb(volatile uint8_t* number, uint8_t mask)
{
	asm volatile("lock andb %1,%0" : "=m"(*number) : "r"(mask) : "cc");
}
#endif /* !ROS_INCLUDE_ATOMIC_H */
