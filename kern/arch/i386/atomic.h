#ifndef ROS_INCLUDE_ATOMIC_H
#define ROS_INCLUDE_ATOMIC_H

#include <arch/types.h>

#define mb() {rmb(); wmb();}
#define rmb() ({ asm volatile("lfence"); })
#define wmb() 

//linux style atomic ops
typedef struct {volatile uint32_t real_num;} atomic_t;
#define atomic_read(atom) ((atom)->real_num)
#define atomic_set(atom, val) (((atom)->real_num) = (val))
#define atomic_init(i) {(i)}
//and the atomic incs, etc take an atomic_t ptr, deref inside

static inline void atomic_inc(atomic_t* number);
static inline void atomic_dec(atomic_t* number);
static inline void atomic_andb(volatile uint8_t* number, uint8_t mask);
static inline void spin_lock(volatile uint32_t* lock);
static inline void spin_unlock(volatile uint32_t* lock);

/* Inlined functions declared above */

// need to do this with pointers and deref.  %0 needs to be the memory address
static inline void atomic_inc(atomic_t* number)
{
	asm volatile("lock incl %0" : "=m"(number->real_num) : : "cc");
}

static inline void atomic_dec(atomic_t* number)
{
	asm volatile("lock decl %0" : "=m"(number->real_num) : : "cc");
}

static inline void atomic_andb(uint8_t* number, uint8_t mask)
{
	asm volatile("lock andb %1,%0" : "=m"(*number) : "r"(mask) : "cc");
}


static inline void spin_lock(volatile uint32_t* lock)
{
	asm volatile(
			"1:                       "
			"	cmpb $0, %0;          "
			"	je 2f;                "
			"	pause;                "
			"	jmp 1b;               "
			"2:                       " 
			"	movb $1, %%al;        "
			"	xchgb %%al, %0;       "
			"	cmpb $0, %%al;        "
			"	jne 1b;               "
	        : : "m"(*lock) : "eax", "cc");
}

static inline void spin_unlock(volatile uint32_t* lock)
{
	*lock = 0;
}

#endif /* !ROS_INCLUDE_ATOMIC_H */
