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

#endif
