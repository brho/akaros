#include <atomic.h>

// This emulates compare and swap by hashing the address into one of
// K buckets, acquiring the lock for that bucket, then performing the
// operation during the critical section.  :-(
bool atomic_comp_swap(uintptr_t *addr, uintptr_t exp_val, uintptr_t new_val)
{
	if (*addr != exp_val)
		return 0;
	
  #define K 17
	static spinlock_t cas_locks[K*HW_CACHE_ALIGN/sizeof(spinlock_t)];

  uintptr_t bucket = (uintptr_t)addr / sizeof(uintptr_t) % K;
	spinlock_t* lock = &cas_locks[bucket*HW_CACHE_ALIGN/sizeof(spinlock_t)];
	
	bool retval = 0;
	spin_lock_irqsave(lock);
	if (*addr == exp_val) {
		atomic_swap(addr, new_val);
		retval = 1;
	}
	spin_unlock_irqsave(lock);
	return retval;
}
