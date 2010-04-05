#ifndef PARLIB_ARCH_H
#define PARLIB_ARCH_H

#include <ros/common.h>

static __inline void
breakpoint(void)
{
	__asm __volatile("int3");
}

static __inline uint64_t
read_tsc(void)
{
	uint64_t tsc;
	__asm __volatile("rdtsc" : "=A" (tsc));
	return tsc;
}

static __inline void
cpu_relax(void)
{
	asm volatile("pause" : : : "memory");
}
#endif /* PARLIB_ARCH_H */
