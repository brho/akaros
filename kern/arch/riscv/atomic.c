#include <atomic.h>

// This emulates compare and swap by hashing the address into one of
// K buckets, acquiring the lock for that bucket, then performing the
// operation during the critical section.  :-(
bool atomic_cas(atomic_t *addr, long exp_val, long new_val)
{
	if ((long)*addr != exp_val)
		return 0;
	
  #define K 17
	/* TODO: not sure if this initialization works. */
	static spinlock_t cas_locks[K*ARCH_CL_SIZE/sizeof(spinlock_t)] =
	                  {SPINLOCK_INITIALIZER_IRQSAVE};

  uintptr_t bucket = (uintptr_t)addr / sizeof(uintptr_t) % K;
	spinlock_t* lock = &cas_locks[bucket*ARCH_CL_SIZE/sizeof(spinlock_t)];
	
	bool retval = 0;
	spin_lock_irqsave(lock);
	if ((long)*addr == exp_val) {
		atomic_swap(addr, new_val);
		retval = 1;
	}
	spin_unlock_irqsave(lock);
	return retval;
}

bool atomic_cas_ptr(void **addr, void *exp_val, void *new_val)
{
	return atomic_cas((atomic_t*)addr, (long)exp_val, (long)new_val);
}

/* Ghetto, copied the regular CAS code... */
bool atomic_cas_u32(uint32_t *addr, uint32_t exp_val, uint32_t new_val)
{
	if (*addr != exp_val)
		return 0;
	
  #define K 17
	/* TODO: not sure if this initialization works. */
	static spinlock_t cas_locks[K*ARCH_CL_SIZE/sizeof(spinlock_t)] =
	                  {SPINLOCK_INITIALIZER_IRQSAVE};

  uintptr_t bucket = (uintptr_t)addr / sizeof(uintptr_t) % K;
	spinlock_t* lock = &cas_locks[bucket*ARCH_CL_SIZE/sizeof(spinlock_t)];
	
	bool retval = 0;
	spin_lock_irqsave(lock);
	if (*addr == exp_val) {
		atomic_swap_u32(addr, new_val);
		retval = 1;
	}
	spin_unlock_irqsave(lock);
	return retval;
}
