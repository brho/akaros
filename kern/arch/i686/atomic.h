#ifndef ROS_INCLUDE_ATOMIC_H
#define ROS_INCLUDE_ATOMIC_H

#include <ros/common.h>
#include <ros/arch/membar.h>
#include <arch/x86.h>
#include <arch/arch.h>
typedef void * RACY atomic_t;
struct spinlock {
	volatile uint32_t RACY rlock;
#ifdef __CONFIG_SPINLOCK_DEBUG__
	void *call_site;	
	uint32_t calling_core;
#endif
};
typedef struct spinlock spinlock_t;
#define SPINLOCK_INITIALIZER {0}

static inline void atomic_init(atomic_t *number, int32_t val);
static inline int32_t atomic_read(atomic_t *number);
static inline void atomic_set(atomic_t *number, int32_t val);
static inline void atomic_add(atomic_t* number, long val);
static inline void atomic_inc(atomic_t *number);
static inline void atomic_dec(atomic_t *number);
static inline long atomic_fetch_and_add(atomic_t *number, long val);
static inline bool atomic_add_not_zero(atomic_t *number, long val);
static inline bool atomic_sub_and_test(atomic_t *number, long val);
static inline void atomic_or(atomic_t *number, int mask);
static inline uint32_t atomic_swap(uint32_t *addr, uint32_t val);
static inline bool atomic_comp_swap(uint32_t *addr, uint32_t exp_val,
                                    uint32_t new_val);
static inline void atomic_andb(volatile uint8_t RACY* number, uint8_t mask);
static inline void atomic_orb(volatile uint8_t RACY* number, uint8_t mask);
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

static inline void atomic_add(atomic_t* number, long val)
{
	asm volatile("lock addl %1,%0" : "=m"(*number) : "r"(val));
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

/* Adds val to number, returning number's original value */
static inline long atomic_fetch_and_add(atomic_t *number, long val)
{
	asm volatile("lock xadd %0,%1" : "=r"(val), "=m"(*number)
	                               : "0"(val), "m"(*number)
	                               : "cc" );
	return val;
}

/* Adds val to number, so long as number was not zero.  Returns TRUE if the
 * operation succeeded (added, not zero), returns FALSE if number is zero. */
static inline bool atomic_add_not_zero(atomic_t *number, long val)
{
	long old_num, new_num;
	do {
		old_num = atomic_read(number);
		if (!old_num)
			return FALSE;
		new_num = old_num + val;
	} while (!atomic_comp_swap((uint32_t*)number, old_num, new_num));
	return TRUE;
}

/* Subtraces val from number, returning True if the new value is 0. */
static inline bool atomic_sub_and_test(atomic_t *number, long val)
{
	bool b;
	asm volatile("lock sub %2,%1; setz %0" : "=r"(b), "=m"(*number)
	                                       : "r"(val), "m"(*number)
	                                       : "cc" );
	return b;
}

static inline void atomic_or(atomic_t *number, int mask)
{
	asm volatile("lock orl %1,%0" : "=m"(*number) : "q"(mask) : "cc");
}

static inline uint32_t atomic_swap(uint32_t *addr, uint32_t val)
{
	// this would work, but its code is bigger, and it's not like the others
	//asm volatile("xchgl %0,(%2)" : "=r"(val) : "0"(val), "r"(addr) : "memory");
	asm volatile("xchgl %0,%1" : "=r"(val), "=m"(*addr) : "0"(val), "m"(*addr));
	return val;
}

/* reusing exp_val for the bool return.  1 (TRUE) for success (like test). */
static inline bool atomic_comp_swap(uint32_t *addr, uint32_t exp_val,
                                    uint32_t new_val)
{
	asm volatile("lock cmpxchgl %4,%1; sete %%al"
	             : "=a"(exp_val), "=m"(*addr)
	             : "m"(*addr), "a"(exp_val), "r"(new_val)
	             : "cc");
	return exp_val;
}

/* Be sure to use "q" for byte operations (compared to longs), since this
 * constrains the asm to use e{a,b,c,d}x instead of esi and edi.  32 bit x86
 * cannot access the lower parts of esi or edi (will get warnings like "no such
 * register %sil or %dil." */
static inline void atomic_andb(volatile uint8_t RACY*number, uint8_t mask)
{
	asm volatile("lock andb %1,%0" : "=m"(*number) : "q"(mask) : "cc");
}

static inline void atomic_orb(volatile uint8_t RACY*number, uint8_t mask)
{
	asm volatile("lock orb %1,%0" : "=m"(*number) : "q"(mask) : "cc");
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
#ifdef __CONFIG_SPINLOCK_DEBUG__
	lock->call_site = (void RACY*CT(1))TC(read_eip());
	lock->calling_core = core_id();
#endif
}

static inline void spin_unlock(spinlock_t *lock)
{
	/* Need to prevent the compiler (and some arches) from reordering older
	 * stores */
	wmb();
	lock->rlock = 0;
}

static inline void spinlock_init(spinlock_t *lock)
#ifdef __CONFIG_SPINLOCK_DEBUG__
WRITES(lock->rlock,lock->call_site,lock->calling_core)
#else
WRITES(lock->rlock)
#endif
{
	lock->rlock = 0;
#ifdef __CONFIG_SPINLOCK_DEBUG__
	lock->call_site = 0;
	lock->calling_core = 0;
#endif
}

#endif /* !ROS_INCLUDE_ATOMIC_H */
