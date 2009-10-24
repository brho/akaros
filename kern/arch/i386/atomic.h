#ifndef ROS_INCLUDE_ATOMIC_H
#define ROS_INCLUDE_ATOMIC_H

/* This enables tracking who last locked a spinlock. */
#define SPINLOCK_DEBUG

#include <ros/common.h>
#include <arch/x86.h>
#include <arch/arch.h>

#define mb() {rmb(); wmb();}
#define rmb() ({ asm volatile("lfence"); })
#define wmb() 
/* Force a wmb, used in cases where an IPI could beat a write, even though
 * write-orderings are respected. */
#define wmb_f() ({ asm volatile("sfence"); })

typedef void * RACY atomic_t;
typedef struct spinlock {
	volatile uint32_t RACY rlock;
#ifdef SPINLOCK_DEBUG
	void *call_site;	
	uint32_t calling_core;
#endif
} spinlock_t;
#define SPINLOCK_INITIALIZER {0}

static inline void atomic_init(atomic_t *number, int32_t val);
static inline int32_t atomic_read(atomic_t *number);
static inline void atomic_set(atomic_t *number, int32_t val);
static inline void atomic_inc(atomic_t *number);
static inline void atomic_dec(atomic_t *number);
static inline void atomic_andb(volatile uint8_t RACY* number, uint8_t mask);
static inline uint32_t spin_locked(spinlock_t *SAFE lock);
static inline void __spin_lock(volatile uint32_t SRACY*CT(1) rlock);
static inline void spin_lock(spinlock_t *lock);
static inline void spin_unlock(spinlock_t *lock);
static inline void spinlock_init(spinlock_t *lock);
void spinlock_debug(spinlock_t *lock);

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

static inline void atomic_andb(volatile uint8_t RACY*number, uint8_t mask)
{
	asm volatile("lock andb %1,%0" : "=m"(*number) : "r"(mask) : "cc");
}

static inline uint32_t spin_locked(spinlock_t *SAFE lock)
{
	// the lock status is the lowest byte of the lock
	return lock->rlock & 0xff;
}

static inline void __spin_lock(volatile uint32_t *rlock)
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
	        : : "m"(*rlock) : "eax", "cc");
}

static inline void spin_lock(spinlock_t *lock)
{
	__spin_lock(&lock->rlock);
#ifdef SPINLOCK_DEBUG
	lock->call_site = (void*)read_eip();
	lock->calling_core = core_id();
#endif
}

static inline void spin_unlock(spinlock_t *lock)
{
	lock->rlock = 0;
}

static inline void spinlock_init(spinlock_t *lock)
{
	lock->rlock = 0;
#ifdef SPINLOCK_DEBUG
	lock->call_site = 0;
	lock->calling_core = 0;
#endif
}

#endif /* !ROS_INCLUDE_ATOMIC_H */
