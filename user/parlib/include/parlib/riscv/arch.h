#pragma once

#include <parlib/common.h>
#include <ros/trapframe.h>
#include <ros/arch/arch.h>

__BEGIN_DECLS

#define internal_function

#define ARCH_CL_SIZE 64

static __inline void
set_stack_pointer(void* sp)
{
	asm volatile ("move sp, %0" : : "r"(sp) : "memory");
}

static inline unsigned long get_stack_pointer(void)
{
	unsigned long sp;

	asm volatile("move %0, sp" : "=r"(sp));
	return sp;
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

static inline void save_fp_state(struct ancillary_state* silly)
{
	uint32_t fsr = read_fsr();

	asm("fsd fs0,%0" : "=m"(silly->fpr[0]));
	asm("fsd fs1,%0" : "=m"(silly->fpr[1]));
	asm("fsd fs2,%0" : "=m"(silly->fpr[2]));
	asm("fsd fs3,%0" : "=m"(silly->fpr[3]));
	asm("fsd fs4,%0" : "=m"(silly->fpr[4]));
	asm("fsd fs5,%0" : "=m"(silly->fpr[5]));
	asm("fsd fs6,%0" : "=m"(silly->fpr[6]));
	asm("fsd fs7,%0" : "=m"(silly->fpr[7]));
	asm("fsd fs8,%0" : "=m"(silly->fpr[8]));
	asm("fsd fs9,%0" : "=m"(silly->fpr[9]));
	asm("fsd fs10,%0" : "=m"(silly->fpr[10]));
	asm("fsd fs11,%0" : "=m"(silly->fpr[11]));
	asm("fsd fs12,%0" : "=m"(silly->fpr[12]));
	asm("fsd fs13,%0" : "=m"(silly->fpr[13]));
	asm("fsd fs14,%0" : "=m"(silly->fpr[14]));
	asm("fsd fs15,%0" : "=m"(silly->fpr[15]));

	silly->fsr = fsr;
}

static inline void restore_fp_state(struct ancillary_state* silly)
{
	uint32_t fsr = silly->fsr;

	asm("fld fs0,%0" : : "m"(silly->fpr[0]));
	asm("fld fs1,%0" : : "m"(silly->fpr[1]));
	asm("fld fs2,%0" : : "m"(silly->fpr[2]));
	asm("fld fs3,%0" : : "m"(silly->fpr[3]));
	asm("fld fs4,%0" : : "m"(silly->fpr[4]));
	asm("fld fs5,%0" : : "m"(silly->fpr[5]));
	asm("fld fs6,%0" : : "m"(silly->fpr[6]));
	asm("fld fs7,%0" : : "m"(silly->fpr[7]));
	asm("fld fs8,%0" : : "m"(silly->fpr[8]));
	asm("fld fs9,%0" : : "m"(silly->fpr[9]));
	asm("fld fs10,%0" : : "m"(silly->fpr[10]));
	asm("fld fs11,%0" : : "m"(silly->fpr[11]));
	asm("fld fs12,%0" : : "m"(silly->fpr[12]));
	asm("fld fs13,%0" : : "m"(silly->fpr[13]));
	asm("fld fs14,%0" : : "m"(silly->fpr[14]));
	asm("fld fs15,%0" : : "m"(silly->fpr[15]));

	write_fsr(fsr);
}

static inline bool arch_has_mwait(void)
{
	return false;
}

__END_DECLS
