#ifndef ROS_INC_ATOMIC_H
#define ROS_INC_ATOMIC_H

#include <inc/types.h>

/* //linux style atomic ops
typedef struct {uint32_t real_num;} atomic_t;
#define atomic_read(atom) ((atom)->real_num)
#define atomic_set(atom, val) (((atom)->real_num) = (val))
#define atomic_init(i) {(i)}
//and the atomic incs, etc take an atomic_t ptr, deref inside
*/

static inline void spin_lock(volatile uint32_t* lock);
static inline void spin_unlock(volatile uint32_t* lock);
static inline void spin_lock_irqsave(volatile uint32_t* lock);
static inline void spin_unlock_irqsave(volatile uint32_t* lock);
static inline void atomic_inc(volatile uint32_t* number);
static inline void atomic_dec(volatile uint32_t* number);


static inline void spin_lock(volatile uint32_t* lock)
{
	asm volatile(
			"spinlock:                "
			"	cmpb $0, %0;          "
			"	je getlock;           "
			"	pause;                "
			"	jmp spinlock;         "
			"getlock:                 " 
			"	movb $1, %%al;        "
			"	xchgb %%al, %0;       "
			"	cmpb $0, %%al;        "
			"	jne spinlock;         "
	        : : "m"(*lock) : "eax", "cc");
}

static inline void spin_unlock(volatile uint32_t* lock)
{
	*lock = 0;
}

// TODO - and test by holding a lock in a while loop, then see if ints are off
// and the other case, etc.
// if ints are enabled, disable them and note it in the top bit of the lock
static inline void spin_lock_irqsave(volatile uint32_t* lock)
{
	// doesn't actually do this yet
	// probably want to push flags, cli, grab lock, examine flags, 
	// and toggle the bit if interrupts were enabled
	asm volatile(
			"spinlock:                "
			"	cmpb $0, %0;          "
			"	je getlock;           "
			"	pause;                "
			"	jmp spinlock;         "
			"getlock:                 " 
			"	movb $1, %%al;        "
			"	xchgb %%al, %0;       "
			"	cmpb $0, %%al;        "
			"	jne spinlock;         "
	        : : "m"(*lock) : "eax", "cc");
}

// if the top bit of the lock is set, then re-enable interrupts (TODO)
static inline void spin_unlock_irqsave(volatile uint32_t* lock)
{
	*lock = 0;
}

// need to do this with pointers and deref.  %0 needs to be the memory address
static inline void atomic_inc(volatile uint32_t* number)
{
	asm volatile("lock incl %0" : "=m"(*number) : : "cc");
}

static inline void atomic_dec(volatile uint32_t* number)
{
	asm volatile("lock decl %0" : "=m"(*number) : : "cc");
}
#endif /* !ROS_INC_ATOMIC_H */
