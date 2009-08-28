#ifndef ROS_INCLUDE_ATOMIC_H
#define ROS_INCLUDE_ATOMIC_H

#include <arch/types.h>

#define mb() {rmb(); wmb();}
#define rmb() ({ asm volatile("lfence"); })
#define wmb() 

typedef void* atomic_t;
typedef volatile uint32_t spinlock_t;

static inline void atomic_init(atomic_t *number, int32_t val);
static inline int32_t atomic_read(atomic_t *number);
static inline void atomic_set(atomic_t *number, int32_t val);
static inline void atomic_inc(atomic_t *number);
static inline void atomic_dec(atomic_t *number);
static inline void atomic_andb(volatile uint8_t* number, uint8_t mask);
static inline void spin_lock(volatile uint32_t*COUNT(1) lock);
static inline void spin_unlock(volatile uint32_t* lock);

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
	asm volatile("lock decl %0" : "=m"(*number) : : "cc");
}

static inline void atomic_andb(volatile uint8_t *number, uint8_t mask)
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
