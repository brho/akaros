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
	asm ("rdnpc %0" : "=r"(pc));
	return pc;
}

static __inline void
send_ipi(uint32_t who)
{
	mtpcr(PCR_SEND_IPI, who);
}

static __inline void
clear_ipi()
{
	mtpcr(PCR_CLR_IPI, 0);
}

#endif
