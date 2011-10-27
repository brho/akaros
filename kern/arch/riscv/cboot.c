#include <multiboot.h>
#include <ros/memlayout.h>
#include <arch/arch.h>
#include <arch/mmu.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <pmap.h>

#define MAX_KERNBASE_SIZE (KERN_VMAP_TOP - KERNBASE)

static uint64_t
mem_size(void)
{
	uint64_t sz = MIN(mfpcr(PCR_MEMSIZE)*PGSIZE, MAX_KERNBASE_SIZE);
#ifndef __riscv64
	// limit memory size for RV32
	sz = MIN(sz, L1PGSIZE*NPTENTRIES);
#endif
	return sz;
}

void pagetable_init(pte_t* l1pt, pte_t* l1pt_boot, pte_t* l2pt)
{
	static_assert(KERNBASE % L1PGSIZE == 0);
	// The boot L1 PT retains the identity mapping [0,memsize-1],
	// whereas the post-boot L1 PT does not.
	uint64_t memsize = mem_size();
	for(uint64_t pa = 0; pa < memsize+L1PGSIZE-1; pa += L1PGSIZE)
	{
		pte_t pte = PTE(LA2PPN(pa), PTE_KERN_RW | PTE_E);

		l1pt_boot[L1X(pa)] = pte; // identity mapping
		l1pt_boot[L1X(KERNBASE+pa)] = pte; // KERNBASE mapping
		l1pt[L1X(KERNBASE+pa)] = pte; // KERNBASE mapping
	}

#ifdef __riscv64
	// The kernel code and static data actually are usually not accessed
	// via the KERNBASE mapping, but rather by an aliased "load" mapping in
	// the upper 2GB (0xFFFFFFFF80000000 and up).
	// This simplifies the linking model by making all static addresses
	// representable in 32 bits.
	static_assert(L1X(KERN_LOAD_ADDR) > L1X(KERNBASE));
	static_assert(KERN_LOAD_ADDR % L2PGSIZE == 0);
	static_assert((uintptr_t)(-KERN_LOAD_ADDR) <= L1PGSIZE);

	l1pt[L1X(KERN_LOAD_ADDR)] = PTD(l2pt);
	l1pt_boot[L1X(KERN_LOAD_ADDR)] = PTD(l2pt);

	for (uintptr_t pa = 0; pa < (uintptr_t)(-KERN_LOAD_ADDR); pa += L2PGSIZE)
		l2pt[L2X(KERN_LOAD_ADDR+pa)] = PTE(LA2PPN(pa), PTE_KERN_RW | PTE_E);
#else
	(void) l2pt; // don't need this for rv32
#endif
}

void
cmain()
{
	multiboot_info_t mbi;
	memset(&mbi, 0, sizeof(mbi));
	mbi.flags = 0x00000001;
	mbi.mem_lower = (unsigned long)(mem_size() / 1024);

	extern void kernel_init(multiboot_info_t *mboot_info);
	// kernel_init expects a pre-relocation mbi address
	kernel_init((multiboot_info_t*)PADDR(&mbi));
}
