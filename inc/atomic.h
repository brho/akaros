#ifndef ROS_INC_ATOMIC_H
#define ROS_INC_ATOMIC_H

#include <inc/types.h>
#include <inc/mmu.h>
#include <inc/x86.h>

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

// If ints are enabled, disable them and note it in the top bit of the lock
// There is an assumption about releasing locks in order here...
static inline void spin_lock_irqsave(volatile uint32_t* lock)
{
	uint32_t eflags;
	eflags = read_eflags();
	disable_irq();
	spin_lock(lock);
	if (eflags & FL_IF)
		*lock |= 0x80000000;
}

// if the top bit of the lock is set, then re-enable interrupts
static inline void spin_unlock_irqsave(volatile uint32_t* lock)
{
	if (*lock & 0x80000000) {
		*lock = 0;
		enable_irq();
	} else
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
