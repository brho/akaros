#ifndef ROS_INC_ARCH_MEMBAR_H
#define ROS_INC_ARCH_MEMBAR_H

/* Full CPU memory barrier */
#define mb() {rmb(); wmb();}
/* Compiler memory barrier (optimization barrier) */
#define cmb() ({ asm volatile("" ::: "memory"); })
/* Partial CPU memory barriers */
#define rmb() cmb()
#define wmb() ({ __asm__ __volatile__ ("stbar" ::: "memory"); })
#define wrmb() mb()
#define rwmb() mb()

/* Forced barriers, used for string ops, SSE ops, dealing with hardware, or
 * other places where you avoid 'normal' x86 read/writes (like having an IPI
 * beat a write) */
#define mb_f() mb()
#define rmb_f() rmb()
#define wmb_f() wmb()
#define wrmb_f() wrmb()
#define rwmb_f() rwmb()

#endif /* ROS_INC_ARCH_MEMBAR_H */
