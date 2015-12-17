#pragma once

#include <ros/arch/mmu.h>

#ifndef __ASSEMBLER__

static inline pte_t build_pte(uintptr_t pa, int flags)
{
	return LA2PPN(pa) << PTE_PPN_SHIFT | PGOFF(flags);
}

#endif /* __ASSEMBLER__ */
