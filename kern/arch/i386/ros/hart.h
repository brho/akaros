#ifndef _ROS_ARCH_HART_H
#define _ROS_ARCH_HART_H

#ifndef ROS_KERNEL

#include <ros/syscall.h>

#define HART_CL_SIZE 64

// this is how we get our thread id on entry.
#define __hart_self_on_entry \
({ \
	register int temp asm ("eax"); \
	temp; \
})

// The actual hart_self() function is a global symbol that invokes this routine.
static inline int
__hart_self()
{
	// TODO: use some kind of thread-local storage to speed this up!
	return (int)ros_syscall(SYS_getvcoreid,0,0,0,0,0);
}

static inline void
__hart_set_stack_pointer(void* sp)
{
	asm volatile ("mov %0,%%esp" : : "r"(sp) : "memory","esp");
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

#endif
