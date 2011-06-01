#include <multiboot.h>
#include <ros/memlayout.h>
#include <arch/arch.h>
#include <arch/mmu.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <pmap.h>

#define KERNSIZE L1PGSIZE

static void
build_multiboot_info(multiboot_info_t* mbi)
{
	long memsize = mfpcr(PCR_MEMSIZE)*PGSIZE;
	// the current memory mapping requires the kernel be mapped by a single
	// L2 page table.
	memsize = MIN(memsize, L1PGSIZE);
	memsize = ROUNDDOWN(memsize, L2PGSIZE);

	long memsize_kb = memsize/1024;
	long basemem_kb = EXTPHYSMEM/1024;

	memset(mbi, 0, sizeof(mbi));

	mbi->flags = 0x00000001;
	mbi->mem_lower = basemem_kb;
	mbi->mem_upper = memsize_kb - basemem_kb;
}

pte_t l1pt_boot[NPTENTRIES]
      __attribute__((section(".data"))) __attribute__((aligned(PGSIZE)));
pte_t l1pt[NPTENTRIES]
      __attribute__((section(".data"))) __attribute__((aligned(PGSIZE)));
#ifdef __riscv64
pte_t l2pt_kernbase[NPTENTRIES]
      __attribute__((section(".data"))) __attribute__((aligned(PGSIZE)));
pte_t l2pt_load[NPTENTRIES]
      __attribute__((section(".data"))) __attribute__((aligned(PGSIZE)));
#endif

#ifdef __riscv64
void pagetable_init(pte_t* l1pt_phys, pte_t* l1pt_boot_phys,
                    pte_t* l2pt_kernbase_phys, pte_t* l2pt_load_phys)
#else
void pagetable_init(pte_t* l1pt_phys, pte_t* l1pt_boot_phys)
#endif
{
	// The boot L1 PT retains the identity mapping [0,KERNSIZE-1],
	// whereas the post-boot L1 PT does not.
	for(uintptr_t va = 0; va < KERNSIZE+L1PGSIZE-1; va += L1PGSIZE)
		l1pt_boot_phys[L1X(va)] = PTE(LA2PPN(va), PTE_KERN_RW | PTE_E);

	#ifdef __riscv64
	// for rv64, we need to create an L1 and an L2 PT.
	
	// kernel can be mapped by a single L2 PT.
	static_assert(L1X(KERNBASE) == L1X(KERNBASE+KERNSIZE-1));
	static_assert(KERNSIZE % L2PGSIZE == 0);
	static_assert(KERNBASE % L2PGSIZE == 0);

	// KERNBASE mapping uses one L1 PTD -> L2 PT
	l1pt_phys[L1X(KERNBASE)]      = PTD(l2pt_kernbase_phys);
	l1pt_boot_phys[L1X(KERNBASE)] = PTD(l2pt_kernbase_phys);

	for(uintptr_t va = KERNBASE; va < KERNBASE+KERNSIZE; va += L2PGSIZE)
		l2pt_kernbase_phys[L2X(va)] = PTE(LA2PPN(va-KERNBASE), PTE_KERN_RW | PTE_E);

	// The kernel code and static data actually are usually not accessed
	// via the KERNBASE mapping, but rather by an aliased "load" mapping in
	// the upper 2GB (0xFFFFFFFF80000000 and up).
	// This simplifies the linking model by making all static addresses
	// representable in 32 bits.
	// In RISC-V, this allows static addresses to be loaded with a two
	// instruction sequence, rather than 8 instructions worst-case.
	static_assert(L1X(KERN_LOAD_ADDR) > L1X(KERNBASE));
	static_assert(KERN_LOAD_ADDR % L2PGSIZE == 0);
	static_assert((uintptr_t)(-KERN_LOAD_ADDR) <= L1PGSIZE);

	l1pt_phys[L1X(KERN_LOAD_ADDR)]      = PTD(l2pt_load_phys);
	l1pt_boot_phys[L1X(KERN_LOAD_ADDR)] = PTD(l2pt_load_phys);

	for(uintptr_t va = KERN_LOAD_ADDR; va != 0; va += L2PGSIZE)
		l2pt_load_phys[L2X(va)] = PTE(LA2PPN(va-KERN_LOAD_ADDR), PTE_KERN_RW|PTE_E);
	#else
	// for rv32, just create the L1 page table.
	static_assert(KERNBASE % L1PGSIZE == 0);

	// KERNBASE mapping
	for(uintptr_t pa = 0; pa < KERNSIZE; pa += L1PGSIZE)
	{
		pte_t pte = PTE(LA2PPN(pa), PTE_KERN_RW|PTE_E);
		l1pt_phys[L1X(KERNBASE+pa)] = pte;
		l1pt_boot_phys[L1X(KERNBASE+pa)] = pte;
	}
	#endif
}

void
mmu_init(pte_t* l1pt_boot_phys)
{
	// load in the boot page table
	lcr3((uintptr_t)l1pt_boot_phys);
	mtpcr(PCR_SR, mfpcr(PCR_SR) | SR_VM);
}

void
cmain()
{
	multiboot_info_t mbi;
	build_multiboot_info(&mbi);

	extern void kernel_init(multiboot_info_t *mboot_info);
	// kernel_init expects a pre-relocation mbi address
	kernel_init((multiboot_info_t*)PADDR(&mbi));
}
