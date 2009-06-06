#ifndef ROS_INC_ATOMIC_H
#define ROS_INC_ATOMIC_H

#include <arch/atomic.h>


/* Ghetto-atomics, stolen from kern/atomic.* */
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

typedef struct barrier {
	volatile uint32_t lock;
	uint32_t init_count;
	uint32_t current_count;
    volatile uint8_t ready;
} barrier_t;

void init_barrier(barrier_t* barrier, uint32_t count);
void reset_barrier(barrier_t* barrier);
void waiton_barrier(barrier_t* barrier);

#endif /* !ROS_INC_ATOMIC_H */
