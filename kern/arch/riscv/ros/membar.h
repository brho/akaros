#ifndef _ARCH_MEMBAR_H
#define _ARCH_MEMBAR_H

#define mb() __sync_synchronize()
#define rmb() mb()
#define wmb() mb()
/* Compiler memory barrier */
#define cmb() ({ asm volatile("" ::: "memory"); })
/* Force a wmb, used in cases where an IPI could beat a write, even though
 * write-orderings are respected. */
#define wmb_f() mb()

#endif
