#include <arch/arch.h>
#include <arch/mmu.h>
#include <stdio.h>
#include <assert.h>
#include <smp.h>
#include <umem.h>
#include <pmap.h>

static void
static_asserts_can_go_here()
{
	static_assert(SIZEOF_HW_TRAPFRAME == sizeof(struct hw_trapframe));
}

void
print_cpuinfo(void)
{
	int id = mfpcr(PCR_IMPL);
	const char* name = "(unknown implementation)";
	if (id == 1)
	  name = "ISA Simulator";
	else if (id == 2)
	  name = "Rocket64";
	cprintf("CPU Info: RISC-V %s\n", name);
}

void show_mapping(pde_t *pt, uintptr_t start, size_t size)
{
	pte_t* pte;
	uintptr_t i;
	page_t* page;

	cprintf("      Virtual            Physical      SR SW SX UR UW UX D R\n");
	cprintf("------------------------------------------------------------\n");
	for(i = 0; i < size; i += PGSIZE, start += PGSIZE)
	{
		page = page_lookup(pt, (void*)start, &pte);
		cprintf("%016p  ",start);
		if(page)
		{
			cprintf("%016p  %1d  %1d  %1d  %1d  %1d  %1d %1d %1d\n",
			        page2pa(page),
			        !!(*pte & PTE_SR), !!(*pte & PTE_SW), !!(*pte & PTE_SX),
			        !!(*pte & PTE_UR), !!(*pte & PTE_UW), !!(*pte & PTE_UX),
			        !!(*pte & PTE_D), !!(*pte & PTE_R));
		}
		else
			cprintf("%016p\n",0);
	}
}
