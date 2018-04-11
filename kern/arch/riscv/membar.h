#include <ros/arch/membar.h>

#error "implement me (grab from linux)"

#define smp_store_release(p, v)
#define smp_load_acquire(p)
#define smp_mb__before_atomic()
#define smp_mb__after_atomic()

#define smp_read_barrier_depends()
