#ifndef _ARCH_MEMBAR_H
#define _ARCH_MEMBAR_H

#define mb() {rmb(); wmb();}
#define rmb() ({ asm volatile("lfence"); })
#define wmb() 
/* Force a wmb, used in cases where an IPI could beat a write, even though
 *  * write-orderings are respected. */
#define wmb_f() ({ asm volatile("sfence"); })

#endif
