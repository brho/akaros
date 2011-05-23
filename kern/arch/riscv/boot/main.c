#include <multiboot.h>
#include <ros/memlayout.h>

static void
build_multiboot_info(multiboot_info_t* mbi)
{
	long memsize_kb = mfpcr(PCR_MEMSIZE)*(PGSIZE/1024);
	long basemem_kb = EXTPHYSMEM/1024;

	memset(mbi, 0, sizeof(mbi));

	mbi->flags = 0x00000001;
	mbi->mem_lower = basemem_kb;
	mbi->mem_upper = memsize_kb - basemem_kb;
}

#ifdef __riscv64
#define NL3PT ((KERNSIZE+L2PGSIZE-1)/L2PGSIZE)
static pte_t l1pt[NL1ENTRIES], l2pt[NL2ENTRIES], l3pts[NL3PT][NL3ENTRIES]
      __attribute__((section("data"))) __attribute__((aligned(PGSIZE)));
#else
static pte_t l1pt[NL1ENTRIES];
#endif

void
mmu_init()
{
	pte_t* l1pt_phys = (pte_t*)((uint8_t*)l1pt - KERNBASE);
	pte_t* l2pt_phys = (pte_t*)((uint8_t*)l2pt - KERNBASE);

	// Retain the identity mapping [0,KERNSIZE]
	for(uintptr_t i = 0; i < (KERNSIZE+L1PGSIZE-1)/L1PGSIZE; i++)
		l1pt_phys[i] = PTE(LA2PPN(i*L1PGSIZE), PTE_KERN_RW|PTE_E);

	#ifdef __riscv64
	// for rv64, we need to create an L1 and an L2 PT, and many L3 PTs.

	// kernel can be mapped by a single L1 page
	static_assert(KERNSIZE <= L1PGSIZE);
	static_assert(KERNBASE % L3PGSIZE == 0);

	// highest L1 page contains KERNBASE mapping
	uintptr_t l1x = L1X(KERNBASE);
	l1pt_phys[l1x] = PTD(l2pt);

	for(uintptr_t i = 0; i < NL3PT; i++)
	{
		uintptr_t l2x = L2X(KERNBASE + i*L2PGSIZE);
		l2pt_phys[l2x] = PTD(l3pts[l2x]);
		for(uintptr_t l3x = 0; l3x < NPTENTRIES; l3x++)
		{
			uintptr_t addr = PGADDR(l1x, l2x, l3x, 0, 0);
			if(addr >= KERNBASE)
				l3pts[l2x][l3x] = PTE(LA2PPN(addr), PTE_KERN_RW | PTE_E);
		}
	}

	// KERNBASE mapping
	l1pt_phys[NPTENTRIES-1] = PTD(l2pt_phys);
	for(uintptr_t i = 0; i < (KERNSIZE+L2PGSIZE-1)/L2PGSIZE; i++)
		l2pt_phys[i] = PTD(l1pt_phys + i*NPTENTRIES);
	
  // Map the upper 2GB (0xFFFFFFFF80000000 and up) to alias the KERNBASE
	// mapping.  We'll use this region to reference static/global variables
	// more efficiently with a LUI/ADD pair, which can only reach addresses
	// 0x00000000->0x7FFFF7FF and 0xFFFFFFFF80000000->0xFFFFFFFFFFFFF7FF.
	// The alternative requires an 8-instruction sequence in the general case.
  uintptr_t start = 0xFFFFFFFF80000000;
	static_assert(start % L2PGSIZE == 0);
	for(uintptr_t i = 0; i < ((uintptr_t)-start)/L2PGSIZE; i++)
	  l2pt[i+start/L2PGSIZE] = PTE(LA2PPN(i*L2PGSIZE), PTE_KERN_RW|PTE_E);
	#else
	// for rv32, just create the L1 page table.
	static_assert(KERNBASE % L1PGSIZE == 0);

	// KERNBASE mapping
	for(uintptr_t i = 0; i < KERNSIZE/L1PGSIZE; i++)
		l1pt_phys[i+KERNBASE/L1PGSIZE] = PTE(LA2PPN(i*L1PGSIZE), PTE_KERN_RW|PTE_E);
	#endif

	lcr3(l1pt_phys);
	mtpcr(PCR_SR, mfpcr(PCR_SR) | SR_VM);
}

static void
mmu_init_cleanup()
{
	// after relocation, we no longer rely on the identity mapping
	for(uintptr_t i = 0; i < (KERNSIZE+L1PGSIZE-1)/L1PGSIZE; i++)
		l1pt_phys[i] = 0;
	tlbflush();
}

void
cmain()
{
	mmu_init_cleanup();

	multiboot_info_t mbi;
	build_multiboot_info(&mbi);

	extern void kernel_init(multiboot_info_t *mboot_info);
	// kernel_init expects a pre-relocation mbi address
	kernel_init((multiboot_info_t*)((uint8_t*)&mbi - KERNBASE));
}
