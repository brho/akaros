#ifndef _ARCH_MEMBAR_H
#define _ARCH_MEMBAR_H

/* Adding "memory" to keep the compiler from fucking with us */
#define mb() ({ asm volatile("mfence" ::: "memory"); })
#define rmb() ({ asm volatile("lfence" ::: "memory"); })
#define wmb() ({ asm volatile("" ::: "memory"); })
/* Compiler memory barrier */
#define cmb() ({ asm volatile("" ::: "memory"); })
/* Force a wmb, used in cases where an IPI could beat a write, even though
 * write-orderings are respected.
 * TODO: this probably doesn't do what you want. */
#define wmb_f() ({ asm volatile("sfence"); })

#endif
