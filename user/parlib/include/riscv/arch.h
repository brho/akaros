#ifndef PARLIB_ARCH_H
#define PARLIB_ARCH_H

#include <ros/common.h>

#define internal_function

#define ARCH_CL_SIZE 64

static __inline void
set_stack_pointer(void* sp)
{
	asm volatile ("move sp, %0" : : "r"(sp) : "memory");
}

static __inline void
breakpoint(void)
{
	asm volatile ("break");
}

static __inline uint64_t
read_tsc(void)
{
	unsigned long cycles;
	asm ("rdcycle %0" : "=r"(cycles));
	return (uint64_t)cycles;
}

static __inline uint64_t
read_tscp(void)
{
	return read_tsc();
}

static __inline uint64_t
read_tsc_serialized(void)
{
	return read_tsc();
}

static __inline void
cpu_relax(void)
{
	long ctr;
	asm volatile("li %0, 8; 1: addi %0, %0, -1; bnez %0, 1b" : "=r"(ctr) : : "memory");
}

static inline void save_fp_state(struct ancillary_state *silly)
{
	/* TODO: (either inlined or in user/parlib/riscv/whatever.c */
}

static inline void restore_fp_state(struct ancillary_state *silly)
{
	/* TODO: (either inlined or in user/parlib/riscv/whatever.c */
}

#endif /* PARLIB_ARCH_H */
