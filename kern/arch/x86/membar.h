#include <ros/arch/membar.h>

/*************************
 * From Linux commit 569dbb88e80d ("Linux 4.13")
 * arch/x86/include/asm/barrier.h
 *
 * Note that their barrier() is our cmb(), but we actually have both available.
 * barrier() comes from e.g. <linux/compiler-gcc.h>.
 */

#define __smp_store_release(p, v)					\
do {									\
	compiletime_assert_atomic_type(*p);				\
	barrier();							\
	WRITE_ONCE(*p, v);						\
} while (0)

#define __smp_load_acquire(p)						\
({									\
	typeof(*p) ___p1 = READ_ONCE(*p);				\
	compiletime_assert_atomic_type(*p);				\
	barrier();							\
	___p1;								\
})

/* Atomic operations are already serializing on x86 */
#define __smp_mb__before_atomic()	barrier()
#define __smp_mb__after_atomic()	barrier()

/************************* End Linux barrier.h */

#define smp_store_release(p, v) __smp_store_release(p, v)
#define smp_load_acquire(p) __smp_load_acquire(p)
#define smp_mb__before_atomic() __smp_mb__before_atomic()
#define smp_mb__after_atomic() __smp_mb__after_atomic()

#define smp_read_barrier_depends()
