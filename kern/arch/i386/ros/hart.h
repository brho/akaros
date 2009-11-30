#ifndef _ROS_ARCH_HART_H
#define _ROS_ARCH_HART_H

#define HART_ALLOCATE_STACKS

#include <parlib.h>

// The actual hart_self() function is a global symbol that invokes this routine.
static inline int
__hart_self()
{
	// TODO: use some kind of thread-local storage to speed this up!
	return (int)syscall(SYS_getvcoreid,0,0,0,0,0);
}

static inline void
__hart_relax()
{
	asm volatile ("pause" : : : "memory");
}

static inline int
__hart_swap(int* addr, int val)
{
	asm volatile ("xchg %0, (%2)" : "=r"(val) : "0"(val),"r"(addr) : "memory");
	return val;
}

#endif
