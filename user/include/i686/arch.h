#ifndef PARLIB_ARCH_H
#define PARLIB_ARCH_H

#include <ros/arch/trapframe.h>
#include <ros/arch/mmu.h>
#include <ros/common.h>
#include <string.h>

#define internal_function   __attribute ((regparm (3), stdcall))

#define ARCH_CL_SIZE 64

static __inline void __attribute__((always_inline))
set_stack_pointer(void* sp)
{
	asm volatile ("mov %0,%%esp" : : "r"(sp) : "memory","esp");
}

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

// This assumes a user_tf looks like a regular kernel trapframe
static __inline void
init_user_tf(struct user_trapframe *u_tf, uint32_t entry_pt, uint32_t stack_top)
{
	memset(u_tf, 0, sizeof(struct user_trapframe));
	u_tf->tf_eip = entry_pt;
	u_tf->tf_cs = GD_UT | 3;
	u_tf->tf_esp = stack_top;
}

#endif /* PARLIB_ARCH_H */
