#include <ros/atomic.h>
#include <stdint.h>
#include <spinlock.h>

static spinlock_t lock = SPINLOCK_INITIALIZER;

#define bool_cas_func(size, type) \
  bool __sync_bool_compare_and_swap_ ## size(type* ptr, type oldval, type newval) \
  { \
	bool success = false; \
	spinlock_lock(&lock); \
	if (*ptr == oldval) \
	{ \
	  *ptr = newval; \
	  success = true; \
	} \
	spinlock_unlock(&lock); \
	return success; \
  }

bool_cas_func(1, uint8_t)
bool_cas_func(2, uint16_t)
bool_cas_func(4, uint32_t)
bool_cas_func(8, uint64_t)
