#pragma once

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

static inline uintptr_t
read_bp(void)
{
	/* frame pointer.  yes, read_bp is a shitty name.  i'll change all of
	 * them to read_fp when you read this and implement the function.  =) */
	return 0;
}

static __inline void
send_ipi(uint32_t who, uint8_t vector)
{
	mtpcr(PCR_SEND_IPI, who);
}

static __inline void
send_broadcast_ipi(uint8_t vector)
{
#warning "broadcast ipi?"
	/* num_cores might not be visible here */
	for (int i = 0; i < num_cores; i++)
		send_ipi(i, vector);
}

static __inline void
clear_ipi()
{
	mtpcr(PCR_CLR_IPI, 0);
}
