#include <arch/sparc.h>
#include <arch/arch.h>
#include <arch/mmu.h>
#include <stdio.h>
#include <assert.h>
#include <smp.h>
#include <umem.h>
#include <pmap.h>

#ifdef __SHARC__
#pragma nosharc
#endif

#ifdef __DEPUTY__
#pragma nodeputy
#endif

void
static_asserts_can_go_here()
{
	static_assert(SIZEOF_TRAPFRAME_T == sizeof(trapframe_t));
	static_assert(SIZEOF_TRAPFRAME_T % 8 == 0);
	static_assert(SIZEOF_KERNEL_MESSAGE_T == sizeof(kernel_message_t));
	static_assert(SIZEOF_KERNEL_MESSAGE_T % 8 == 0);
	static_assert(offsetof(env_t,env_tf) % 8 == 0);
	static_assert(offsetof(env_t,env_ancillary_state) % 8 == 0);
}

void
print_cpuinfo(void)
{
	uint32_t psr = read_psr();
	uint32_t wim = read_wim();
	uint32_t tbr = read_tbr();

	uint32_t mmucr  = read_mmu_reg(MMU_REG_CTRL);
	uint32_t mmuctp = read_mmu_reg(MMU_REG_CTXTBL);
	uint32_t mmuctx = read_mmu_reg(MMU_REG_CTX);
	uint32_t mmufsr = read_mmu_reg(MMU_REG_FSR);
	uint32_t mmufar = read_mmu_reg(MMU_REG_FAR);

	cprintf("CPU Info:\n");
	cprintf("ISA:             SPARC V8\n");
	cprintf("Number of cores: %d\n",num_cpus);
	cprintf("Implementation:  0x%x\n",(psr >> 28) & 0xF);
	cprintf("Version:         0x%x\n",(psr >> 24) & 0xF);
	cprintf("Current PSR:     0x%08x\n",psr);
	cprintf("Current WIM:     0x%08x\n",wim);
	cprintf("Current TBR:     0x%08x\n",tbr);

	cprintf("SRMMU Info:\n");
	cprintf("Implementation:  0x%x\n",(mmucr >> 28) & 0xF);
	cprintf("Version:         0x%x\n",(mmucr >> 24) & 0xF);
	cprintf("Current CR:      0x%08x\n",mmucr);
	cprintf("Current CTP:     0x%08x\n",mmuctp);
	cprintf("Current CTX:     0x%08x\n",mmuctx);
	cprintf("Current FSR:     0x%08x\n",mmufsr);
	cprintf("Current FAR:     0x%08x\n",mmufar);
}

void show_mapping(uintptr_t start, size_t size)
{
	extern pde_t l1_page_table[NL1ENTRIES];
	pte_t* pte;
	uintptr_t i;
	page_t* page;

	cprintf("   Virtual    Physical  C M R ACC P\n");
	cprintf("------------------------------------------\n");
	for(i = 0; i < size; i += PGSIZE, start += PGSIZE)
	{
		page = page_lookup(l1_page_table,(void*)start,&pte);
		cprintf("%08p  ",start);
		if(page)
		{
			cprintf("%08p  %1d %1d %1d  %1x  %1d\n",page2pa(page),
			        !!(*pte & PTE_C),!!(*pte & PTE_M),
			        !!(*pte & PTE_R),(*pte & PTE_ACC) >> 2,
			        !!(*pte & PTE_PTE));
		}
		else
			cprintf("%08p\n",0);
	}
}

void
backtrace(void)
{
	int i = 0, j;

	flush_windows();

	cprintf("Backtrace:\n");

	// hack: assumes (correctly) we aren't a leaf routine
	void *sp, *pc, *newsp;
	__asm__ __volatile__ ("mov %%sp,%0" : "=r"(sp));

	assert(sp >= (void*)KERNBASE);

	newsp = *((void**)sp+14);
	pc = *((void**)sp+15);

	cprintf("initial sp = %x, newsp = %x, pc = %x\n",sp,newsp,pc);
	assert(newsp >= (void*)KERNBASE);

	while(newsp)
	{
		cprintf("#%02d [<%x>]:\n",++i,pc);
		cprintf("    %%sp: %x   Args:",newsp);
		for(j = 8; j < 14; j++)
			cprintf(" %x",*((void**)sp+j));
		cprintf("\n");

		sp = newsp;

		if(sp >= (void*)KERNBASE && (void**)sp+16 > ((void**)0+16))
		{
			newsp = *((void**)sp+14);
			pc = *((void**)sp+15);
		}
		else if(current)
		{
			error_t ret;
			ret  = memcpy_from_user(current,&newsp,(void**)sp+14,sizeof(void*));
			ret |= memcpy_from_user(current,&pc,(void**)sp+15,sizeof(void*));
			if(ret)
			{
				warn("Backtrace would have caused access exception; corrupt user stack?");
				break;
			}
		}
		else
			break;
	}
}
