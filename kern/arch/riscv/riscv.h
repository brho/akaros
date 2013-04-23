#ifndef ROS_INC_RISCV_H
#define ROS_INC_RISCV_H

#include <arch/pcr.h>

static __inline void
lcr3(uintptr_t val)
{
	mtpcr(PCR_PTBR, val);
}

static __inline uintptr_t
rcr3(void)
{
	return mfpcr(PCR_PTBR);
}

static __inline uintptr_t
read_pc(void)
{
	uintptr_t pc;
	asm ("rdpc %0" : "=r"(pc));
	return pc;
}

static __inline void
send_ipi(uint32_t who, uint8_t vector)
{
	mtpcr(PCR_SEND_IPI, who);
}

static __inline void
clear_ipi()
{
	mtpcr(PCR_CLR_IPI, 0);
}

static __inline uint32_t
read_fsr(void)
{
	uint32_t fsr;
	asm volatile ("mffsr %0" : "=r"(fsr));
	return fsr;
}

static __inline void
write_fsr(uint32_t fsr)
{
	asm volatile ("mtfsr %0" :: "r"(fsr));
}

#endif
