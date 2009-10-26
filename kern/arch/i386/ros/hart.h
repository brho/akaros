#ifndef _ROS_ARCH_HART_H
#define _ROS_ARCH_HART_H

#include <parlib.h>

// The actual hart_self() function is a global symbol that invokes this routine.
static inline size_t
__hart_self()
{
	// TODO: use some kind of thread-local storage to speed this up!
	return (size_t)syscall(SYS_getvcoreid,0,0,0,0,0);
}

static inline void
hart_relax()
{
	asm volatile ("pause" : : : "memory");
}

static inline size_t
hart_swap(size_t* addr, size_t val)
{
	asm volatile ("xchg %0, (%2)" : "=r"(val) : "0"(val),"r"(addr) : "memory");
	return val;
}

#endif
