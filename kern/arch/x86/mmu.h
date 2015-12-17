#pragma once

#include <ros/arch/mmu.h>

#ifndef __ASSEMBLER__

static inline kpte_t build_kpte(uintptr_t pa, int flags)
{
	return LA2PPN(pa) << PGSHIFT | PGOFF(flags);
}

#endif /* __ASSEMBLER__ */
