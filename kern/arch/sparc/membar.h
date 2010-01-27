#ifndef _ARCH_MEMBAR_H
#define _ARCH_MEMBAR_H

#define mb() {rmb(); wmb();}
#define rmb()
#define wmb() ({ __asm__ __volatile__ ("stbar"); })
/* Force a wmb, used in cases where an IPI could beat a write, even though
 *  * write-orderings are respected.  (Used by x86) */
#define wmb_f() wmb()

#endif
