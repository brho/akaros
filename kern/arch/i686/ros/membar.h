#ifndef ROS_INC_ARCH_MEMBAR_H
#define ROS_INC_ARCH_MEMBAR_H

/* Full CPU memory barrier */
#define mb() ({ asm volatile("mfence" ::: "memory"); })
/* Compiler memory barrier (optimization barrier) */
#define cmb() ({ asm volatile("" ::: "memory"); })
/* Partial CPU memory barriers */
#define rmb() cmb()
#define wmb() cmb()
#define wrmb() mb()
#define rwmb() cmb()

/* Forced barriers, used for string ops, SSE ops, dealing with hardware, or
 * other places where you avoid 'normal' x86 read/writes (like having an IPI
 * beat a write) */
#define mb_f() ({ asm volatile("mfence" ::: "memory"); })
#define rmb_f() ({ asm volatile("lfence" ::: "memory"); })
#define wmb_f() ({ asm volatile("sfence" ::: "memory"); })
#define wrmb_f() mb_f()
#define rwmb_f() mb_f()

#endif /* ROS_INC_ARCH_MEMBAR_H */
