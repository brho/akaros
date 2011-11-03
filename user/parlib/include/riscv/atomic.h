#ifndef PARLIB_ARCH_ATOMIC_H
#define PARLIB_ARCH_ATOMIC_H

/* Unlike in x86, we need to include spinlocks in the user atomic ops file.
 * Since compare and swap isn't truely non-blocking, and we can't disable
 * interrupts in userspace, there is a slight chance of deadlock. */

#include <ros/common.h>
#include <ros/atomic.h>
#include <ros/arch/membar.h>

typedef struct
{
	volatile uint32_t rlock;
} spinlock_t;

#define SPINLOCK_INITIALIZER {0}

static inline void atomic_init(atomic_t *number, long val);
static inline long atomic_read(atomic_t *number);
static inline void atomic_set(atomic_t *number, long val);
static inline void atomic_inc(atomic_t *number);
static inline void atomic_dec(atomic_t *number);
static inline long atomic_fetch_and_add(atomic_t *number, long val);
static inline long atomic_swap(atomic_t *addr, long val);
static inline void *atomic_swap_ptr(void **addr, void *val);
static inline uint32_t atomic_swap_u32(uint32_t *addr, uint32_t val);
static inline bool atomic_cas(atomic_t *addr, long exp_val, long new_val);
static inline bool atomic_cas_ptr(void **addr, void *exp_val, void *new_val);
static inline bool atomic_cas_u32(uint32_t *addr, uint32_t exp_val,
                                  uint32_t new_val);
static inline void atomic_or_int(volatile int *number, int mask);
static inline uint32_t spin_trylock(spinlock_t* lock);
static inline uint32_t spin_locked(spinlock_t* lock);
static inline void spin_lock(spinlock_t* lock);
static inline void spin_unlock(spinlock_t* lock);

/* Inlined functions declared above */

static inline void atomic_init(atomic_t *number, long val)
{
	*(volatile long*)number = val;
}

static inline long atomic_read(atomic_t *number)
{
	return *(volatile long*)number;
}

/* Sparc needs atomic add, but the regular ROS atomic add conflicts with
 * glibc's internal one. */
static inline void ros_atomic_add(atomic_t *number, long inc)
{
	atomic_fetch_and_add(number, inc);
}

static inline void atomic_set(atomic_t *number, long val)
{
	atomic_init(number, val);
}

static inline void atomic_inc(atomic_t *number)
{
	ros_atomic_add(number, 1);
}

static inline void atomic_dec(atomic_t *number)
{
	ros_atomic_add(number, -1);
}

/* Adds val to number, returning number's original value */
static inline long atomic_fetch_and_add(atomic_t *number, long val)
{
	return __sync_fetch_and_add((long*)number, val);
}

static inline long atomic_swap(atomic_t *addr, long val)
{
	return __sync_lock_test_and_set((long*)addr, val);
}

static inline void *atomic_swap_ptr(void **addr, void *val)
{
	return __sync_lock_test_and_set(addr, val);
}

static inline uint32_t atomic_swap_u32(uint32_t *addr, uint32_t val)
{
	return __sync_lock_test_and_set(addr, val);
}

asm (".section .gnu.linkonce.b.__riscv_ros_atomic_lock, \"aw\", %nobits\n"
     "\t.previous");

spinlock_t __riscv_ros_atomic_lock
  __attribute__ ((nocommon, section (".gnu.linkonce.b.__riscv_ros_atomic_lock"
                                     __sec_comment),
                  visibility ("hidden")));

static inline bool atomic_cas(atomic_t *addr, long exp_val, long new_val)
{
	bool retval = 0;
	long temp;

	if ((long)*addr != exp_val)
		return 0;
	spin_lock(&__riscv_ros_atomic_lock);
	if ((long)*addr == exp_val) {
		atomic_swap(addr, new_val);
		retval = 1;
	}
	spin_unlock(&__riscv_ros_atomic_lock);
	return retval;
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

static inline void atomic_or_int(volatile int *number, int mask)
{
	return __sync_fetch_and_or(number, mask);
}

static inline uint32_t spin_trylock(spinlock_t* lock)
{
	return __sync_fetch_and_or(&lock->rlock, 1) & 1;
}

static inline uint32_t spin_locked(spinlock_t* lock)
{
	return lock->rlock & 1;
}

static inline void spin_lock(spinlock_t* lock)
{
	while(spin_trylock(lock))
		while(spin_locked(lock));
	mb();
}

static inline void spin_unlock(spinlock_t* lock)
{
	mb();
	lock->rlock = 0;
}

static inline void spinlock_init(spinlock_t* lock)
{
	lock->rlock = 0;
}

#endif /* !PARLIB_ARCH_ATOMIC_H */
