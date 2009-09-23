#include <multiboot.h>
#include <arch/mmu.h>
#include <arch/arch.h>
#include <ros/common.h>
#include <ros/memlayout.h>
#include <string.h>

#ifdef __SHARC__
#pragma nosharc
#endif

#ifdef __DEPUTY__
#pragma nodeputy
#endif

extern uintptr_t mmu_context_table[NCONTEXTS];

void
build_multiboot_info(multiboot_info_t* mbi)
{
	uint32_t memsize_kb = memsize_mb()*1024;
	uint32_t basemem_kb = EXTPHYSMEM/1024;

	memset(mbi,0,sizeof(mbi));

	mbi->flags = 0x00000001;
	mbi->mem_lower = basemem_kb;
	mbi->mem_upper = memsize_kb-basemem_kb;
}

// set up a basic virtual -> physical mapping so we can boot the kernel
void
build_boot_pgdir(void)
{
	extern uintptr_t l1_page_table[NL1ENTRIES];

	// relocate symbols
	uintptr_t* mmuctxtbl = (uintptr_t*)((uint8_t*)mmu_context_table-KERNBASE);
	uintptr_t* l1 = (uintptr_t*)((uint8_t*)l1_page_table-KERNBASE);

	uintptr_t kernsize = /* 4GB */ - KERNBASE;

	// make all context table entries invalid
	int i;
	for(i = 0; i < NCONTEXTS; i++)
		mmuctxtbl[i] = 0;

	// except for the zeroth one, which points to our L1 PT
	*mmuctxtbl = PTD((uintptr_t)l1);

	// make all L1 PTEs invalid by default
	for(i = 0; i < NL1ENTRIES; i++)
		l1[i] = 0;

	// Retain the identity mapping
	// [0,4GB-KERNBASE] -> [0,4GB-KERNBASE]
	// so we don't nuke ourselveswhen we turn on protection!!
	for(i = 0; i < kernsize/L1PGSIZE; i++)
		l1[i] = (i << 20) | PTE_KERN_RW | PTE_PTE;

	// make the relocated mapping
	// [KERNBASE,4GB] -> [0,4GB-KERNBASE]
	for(i = 0; i < kernsize/L1PGSIZE; i++)
		l1[i+KERNBASE/L1PGSIZE] = (i << 20) | PTE_KERN_RW | PTE_PTE;
}

void
mmu_init(void)
{
	int zero = 0;
	uintptr_t* mmuctxtbl = (uintptr_t*)((uint8_t*)mmu_context_table-KERNBASE);

	// set physical address of context table
	store_alternate(0x100,4,(uintptr_t)mmuctxtbl>>4);

	// set current context (== 0)
	store_alternate(0x200,4,zero);

	// turn on MMU
	store_alternate(0x000,4,1);

	tlbflush();
}

// delete temporary mappings used by the entry code
void
mmu_boot_cleanup(void)
{
	extern uintptr_t l1_page_table[NL1ENTRIES];
	uintptr_t kernsize = -KERNBASE;

	// make the temporary mapping invalid
	int i;
	for(i = 0; i < kernsize/L1PGSIZE; i++)
		l1_page_table[i] = 0;
}

void
mmu_boot(void)
{
	int id = core_id(), i, ncores = num_cores();

	static volatile int barrier[MAX_NUM_CPUS] = {0};
	static volatile int done_0 = 0, done1 = 0;
	volatile int* done0 = (int*)((uintptr_t)&done_0 - KERNBASE); 

	if(id == 0)
	{
		build_boot_pgdir();
		*done0 = 1;
	}
	else
		while(!*done0);

	mmu_init();

	extern void relocate(void);
	relocate();

	if(id == 0)
	{
		for(i = 1; i < ncores; i++)
			while(!barrier[i]);
		mmu_boot_cleanup();
		done1 = 1;
	}
	else
	{
		barrier[id] = 1;
		while(!done1);
	}

	tlbflush();
}
