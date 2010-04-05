#ifndef PARLIB_ARCH_H
#define PARLIB_ARCH_H

static __inline void
breakpoint(void)
{
	asm volatile ("ta 0x7f");
}

static __inline uint64_t
read_tsc(void)
{
	return read_perfctr(0,0);
}

static __inline void
cpu_relax(void)
{
	int ctr = 8;
	asm volatile("1: deccc %0; bne 1b; nop" :
	             "=r"(ctr) : "0"(ctr) : "cc","memory");
}
#endif /* PARLIB_ARCH_H */
