#ifdef __SHARC__
#pragma nosharc
#define SINIT(x) x
#endif

/* See COPYRIGHT for copyright information. */
#include <arch/x86.h>
#include <arch/arch.h>
#include <arch/mmu.h>
#include <arch/apic.h>

#include <error.h>
#include <sys/queue.h>

#include <atomic.h>
#include <string.h>
#include <assert.h>
#include <pmap.h>
#include <kclock.h>
#include <env.h>
#include <stdio.h>
#include <kmalloc.h>
#include <page_alloc.h>

// These variables are set in i386_vm_init()
pde_t* boot_pgdir;		// Virtual address of boot time page directory
physaddr_t RO boot_cr3;		// Physical address of boot time page directory

// Global descriptor table.
//
// The kernel and user segments are identical (except for the DPL).
// To load the SS register, the CPL must equal the DPL.  Thus,
// we must duplicate the segments for the user and the kernel.
//
segdesc_t gdt_in_c[] =
{
	// 0x0 - unused (always faults -- for trapping NULL far pointers)
	SEG_NULL,

	// 0x8 - kernel code segment
	[GD_KT >> 3] = SEG(STA_X | STA_R, 0x0, 0xffffffff, 0),

	// 0x10 - kernel data segment
	[GD_KD >> 3] = SEG(STA_W, 0x0, 0xffffffff, 0),

	// 0x18 - user code segment
	[GD_UT >> 3] = SEG(STA_X | STA_R, 0x0, 0xffffffff, 3),

	// 0x20 - user data segment
	[GD_UD >> 3] = SEG(STA_W, 0x0, 0xffffffff, 3),

	// 0x28 - tss, initialized in idt_init()
	[GD_TSS >> 3] = SEG_NULL,

	// 0x30 - LDT, set per-process
	[GD_LDT >> 3] = SEG_NULL
};

/* Want gdt to be a pointer, not an array type (can replace it more easily) */
segdesc_t *gdt = gdt_in_c;

pseudodesc_t gdt_pd = {
	sizeof(gdt_in_c) - 1, (unsigned long) gdt_in_c
};

// --------------------------------------------------------------
// Set up initial memory mappings and turn on MMU.
// --------------------------------------------------------------

static void check_boot_pgdir(bool pse);

//
// Map [la, la+size) of linear address space to physical [pa, pa+size)
// in the page table rooted at pgdir.  Size is a multiple of PGSIZE.
// Use permission bits perm|PTE_P for the entries.
//
// To map with Jumbos, set PTE_PS in perm
static void
boot_map_segment(pde_t *COUNT(NPDENTRIES) pgdir, uintptr_t la, size_t size, physaddr_t pa, int perm)
{
	uintptr_t i;
	pte_t *pte;
	// la can be page unaligned, but weird things will happen
	// unless pa has the same offset.  pa always truncates any
	// possible offset.  will warn.  size can be weird too. 
	if (PGOFF(la)) {
		warn("la not page aligned in boot_map_segment!");
		size += PGOFF(la);
	}
	if (perm & PTE_PS) {
		if (JPGOFF(la) || JPGOFF(pa))
			panic("Tried to map a Jumbo page at an unaligned address!");
		// need to index with i instead of la + size, in case of wrap-around
		for (i = 0; i < size; i += JPGSIZE, la += JPGSIZE, pa += JPGSIZE) {
			pte = pgdir_walk(pgdir, (void*)la, 2);
			assert(pte);
			*pte = PTE_ADDR(pa) | PTE_P | perm;
		}
	} else {
		for (i = 0; i < size; i += PGSIZE, la += PGSIZE, pa += PGSIZE) {
			pte = pgdir_walk(pgdir, (void*)la, 1);
			assert(pte);
			if (*pte & PTE_PS)
				// if we start using the extra flag for PAT, which we aren't,
				// this will warn, since PTE_PS and PTE_PAT are the same....
				warn("Possibly attempting to map a regular page into a Jumbo PDE");
			*pte = PTE_ADDR(pa) | PTE_P | perm;
		}
	}
}

// Set up a two-level page table:
//    boot_pgdir is its linear (virtual) address of the root
//    boot_cr3 is the physical adresss of the root
// Then turn on paging.  Then effectively turn off segmentation.
// (i.e., the segment base addrs are set to zero).
// 
// This function only sets up the kernel part of the address space
// (ie. addresses >= ULIM).  The user part of the address space
// will be setup later.
//
// From UWLIM to ULIM, the user is allowed to read but not write.
// Above ULIM the user cannot read (or write). 
void
vm_init(void)
{
	pde_t* pgdir;
	uint32_t cr0, edx;
	size_t n;
	bool pse;

	pse = enable_pse();
	if (pse)
		cprintf("PSE capability detected.\n");

	// we paniced earlier if we don't support PGE.  turn it on now.
	// it's used in boot_map_segment, which covers all of the mappings that are
	// the same for all address spaces.  and also for the VPT mapping below.
	lcr4(rcr4() | CR4_PGE);

	// set up mtrr's for core0.  other cores will do the same later
	setup_default_mtrrs(0);

	/*
	 * PSE status: 
	 * - can walk and set up boot_map_segments with jumbos but can't
	 *   insert yet.  need to look at the page_dir and friends.
	 * - anything related to a single struct page still can't handle 
	 *   jumbos.  will need to think about and adjust Page functions
	 * - do we want to store info like this in the struct page?  or just check
	 *   by walking the PTE
	 * - when we alloc a page, and we want it to be 4MB, we'll need
	 *   to have contiguous memory, etc
	 * - there's a difference between having 4MB page table entries
	 *   and having 4MB Page tracking structs.  changing the latter will
	 *   break a lot of things
	 * - showmapping and friends work on a 4KB granularity, but map to the
	 *   correct entries
	 * - need to not insert / boot_map a single page into an area that is 
	 *   already holding a jumbo page.  will need to break the jumbo up so that
	 *   we can then insert the lone page.  currently warns.
	 * - some inherent issues with the pgdir_walks returning a PTE, and we
	 *   don't know whether it is a jumbo (PDE) or a regular PTE.
	 */

	//////////////////////////////////////////////////////////////////////
	// create initial page directory.
	pgdir = kpage_zalloc_addr();
	assert(pgdir);
	boot_pgdir = pgdir;
	boot_cr3 = PADDR(pgdir);
	// helpful if you want to manually walk with kvm / bochs
	//printk("pgdir va = %p, pgdir pa = %p\n\n", pgdir, PADDR(pgdir));

	//////////////////////////////////////////////////////////////////////
	// Recursively insert PD in itself as a page table, to form
	// a virtual page table at virtual address VPT.
	// (For now, you don't have understand the greater purpose of the
	// following two lines.  Unless you are eagle-eyed, in which case you
	// should already know.)

	// Permissions: kernel RW, user NONE, Global Page
	pgdir[PDX(VPT)] = PADDR(pgdir) | PTE_W | PTE_P | PTE_G;

	// same for UVPT
	// Permissions: kernel R, user R, Global Page
	pgdir[PDX(UVPT)] = PADDR(pgdir) | PTE_U | PTE_P | PTE_G;

	//////////////////////////////////////////////////////////////////////
	// Map all of physical memory at KERNBASE. 
	// Ie.  the VA range [KERNBASE, 2^32) should map to
	//      the PA range [0, 2^32 - KERNBASE)
	// We might not have 2^32 - KERNBASE bytes of physical memory, but
	// we just set up the mapping anyway.
	// Permissions: kernel RW, user NONE
	// Your code goes here: 
	
	// this maps all of the possible phys memory
	// note the use of unsigned underflow to get size = 0x40000000
	//boot_map_segment(pgdir, KERNBASE, -KERNBASE, 0, PTE_W);
	// but this only maps what is available, and saves memory.  every 4MB of
	// mapped memory requires a 2nd level page: 2^10 entries, each covering 2^12
	// need to modify tests below to account for this
	if (pse) {
		// map the first 4MB as regular entries, to support different MTRRs
		boot_map_segment(pgdir, KERNBASE, JPGSIZE, 0, PTE_W | PTE_G);
		boot_map_segment(pgdir, KERNBASE + JPGSIZE, max_paddr - JPGSIZE, JPGSIZE,
		                 PTE_W | PTE_G | PTE_PS);
	} else
		boot_map_segment(pgdir, KERNBASE, max_paddr, 0, PTE_W | PTE_G);

	// APIC mapping: using PAT (but not *the* PAT flag) to make these type UC
	// IOAPIC
	boot_map_segment(pgdir, IOAPIC_BASE, APIC_SIZE, IOAPIC_PBASE,
	                 PTE_PCD | PTE_PWT | PTE_W | PTE_G);
	// Local APIC
	boot_map_segment(pgdir, LAPIC_BASE, APIC_SIZE, LAPIC_PBASE,
	                 PTE_PCD | PTE_PWT | PTE_W | PTE_G);

	// Check that the initial page directory has been set up correctly.
	check_boot_pgdir(pse);

	//////////////////////////////////////////////////////////////////////
	// On x86, segmentation maps a VA to a LA (linear addr) and
	// paging maps the LA to a PA.  I.e. VA => LA => PA.  If paging is
	// turned off the LA is used as the PA.  Note: there is no way to
	// turn off segmentation.  The closest thing is to set the base
	// address to 0, so the VA => LA mapping is the identity.

	// Current mapping: VA KERNBASE+x => PA x.
	//     (segmentation base=-KERNBASE and paging is off)

	// From here on down we must maintain this VA KERNBASE + x => PA x
	// mapping, even though we are turning on paging and reconfiguring
	// segmentation.

	// Map VA 0:4MB same as VA KERNBASE, i.e. to PA 0:4MB.
	// (Limits our kernel to <4MB)
	/* They mean linear address 0:4MB, and the kernel < 4MB is only until 
	 * segmentation is turned off.
	 * once we turn on paging, segmentation is still on, so references to
	 * KERNBASE+x will get mapped to linear address x, which we need to make 
	 * sure can map to phys addr x, until we can turn off segmentation and
	 * KERNBASE+x maps to LA KERNBASE+x, which maps to PA x, via paging
	 */
	pgdir[0] = pgdir[PDX(KERNBASE)];

	// Install page table.
	lcr3(boot_cr3);

	// Turn on paging.
	cr0 = rcr0();
	// CD and NW should already be on, but just in case these turn on caching
	cr0 |= CR0_PE|CR0_PG|CR0_AM|CR0_WP|CR0_NE|CR0_MP;
	cr0 &= ~(CR0_TS|CR0_EM|CR0_CD|CR0_NW);
	lcr0(cr0);

	// Current mapping: KERNBASE+x => x => x.
	// (x < 4MB so uses paging pgdir[0])

	// Reload all segment registers.
	asm volatile("lgdt gdt_pd");
	asm volatile("movw %%ax,%%gs" :: "a" (GD_UD|3));
	asm volatile("movw %%ax,%%fs" :: "a" (GD_UD|3));
	asm volatile("movw %%ax,%%es" :: "a" (GD_KD));
	asm volatile("movw %%ax,%%ds" :: "a" (GD_KD));
	asm volatile("movw %%ax,%%ss" :: "a" (GD_KD));
	asm volatile("ljmp %0,$1f\n 1:\n" :: "i" (GD_KT));  // reload cs
	asm volatile("lldt %%ax" :: "a" (0));

	// Final mapping: KERNBASE+x => KERNBASE+x => x.

	// This mapping was only used after paging was turned on but
	// before the segment registers were reloaded.
	pgdir[0] = 0;

	// Flush the TLB for good measure, to kill the pgdir[0] mapping.
	tlb_flush_global();
	core_id_ready = TRUE;
}

void x86_cleanup_bootmem(void)
{
	#define trampoline_pg 0x00001000UL
	// Remove the mapping of the page used by the trampoline
	page_remove(boot_pgdir, (void*)trampoline_pg);
	// Remove the page table used for that mapping
	pagetable_remove(boot_pgdir, (void*)trampoline_pg);
}

//
// Checks that the kernel part of virtual address space
// has been setup roughly correctly(by i386_vm_init()).
//
// This function doesn't test every corner case,
// in fact it doesn't test the permission bits at all,
// but it is a pretty good sanity check. 
//
static physaddr_t check_va2pa(pde_t *COUNT(NPDENTRIES) pgdir, uintptr_t va);

static void
check_boot_pgdir(bool pse)
{
	uint32_t i, n;
	pde_t *pgdir, pte;

	pgdir = boot_pgdir;

	// check phys mem
	//for (i = 0; KERNBASE + i != 0; i += PGSIZE)
	// adjusted check to account for only mapping avail mem
	if (pse)
		for (i = 0; i < max_paddr; i += JPGSIZE)
			assert(check_va2pa(pgdir, KERNBASE + i) == i);
	else
		for (i = 0; i < max_paddr; i += PGSIZE)
			assert(check_va2pa(pgdir, KERNBASE + i) == i);

	// check for zero/non-zero in PDEs
	for (i = 0; i < NPDENTRIES; i++) {
		switch (i) {
		case PDX(VPT):
		case PDX(UVPT):
		case PDX(LAPIC_BASE): // LAPIC mapping.  TODO: remove when MTRRs are up
			assert(pgdir[i]);
			break;
		default:
			//if (i >= PDX(KERNBASE))
			// adjusted check to account for only mapping avail mem
			// and you can't KADDR maxpa (just above legal range)
			// max_paddr can be up to maxpa, so assume the worst
			if (i >= PDX(KERNBASE) && i <= PDX(KADDR(max_paddr-1)))
				assert(pgdir[i]);
			else
				assert(pgdir[i] == 0);
			break;
		}
	}

	/* check permissions
	 * user read-only.  check for user and write, should be only user
	 * eagle-eyed viewers should be able to explain the extra cases.
	 * for the mongoose-eyed, remember that weird shit happens when you loop
	 * through UVPT.  Specifically, you can't loop once, then look at a jumbo
	 * page that is kernel only.  That's the end of the page table for you, so
	 * having a U on the entry doesn't make sense.  Thus we check for a jumbo
	 * page, and special case it.  This will happen at 0xbf701000.  Why is this
	 * magical?  Get your eagle glasses and figure it out. */
	for (i = UWLIM; i < ULIM; i+=PGSIZE) {
		pte = get_va_perms(pgdir, (void*SAFE)TC(i));
		if (pte & PTE_P) {
			if (i == UVPT+(VPT >> 10))
				continue;
			if (*pgdir_walk(pgdir, (void*SAFE)TC(i), 0) & PTE_PS) {
				assert((pte & PTE_U) != PTE_U);
				assert((pte & PTE_W) != PTE_W);
			} else {
				assert((pte & PTE_U) == PTE_U);
				assert((pte & PTE_W) != PTE_W);
			}
		}
	}
	// kernel read-write.
	for (i = ULIM; i <= KERNBASE + max_paddr - PGSIZE; i+=PGSIZE) {
		pte = get_va_perms(pgdir, (void*SAFE)TC(i));
		if ((pte & PTE_P) && (i != VPT+(UVPT>>10))) {
			assert((pte & PTE_U) != PTE_U);
			assert((pte & PTE_W) == PTE_W);
		}
	}
	// special mappings
	pte = get_va_perms(pgdir, (void*SAFE)TC(UVPT+(VPT>>10)));
	assert((pte & PTE_U) != PTE_U);
	assert((pte & PTE_W) != PTE_W);

	// note this means the kernel cannot directly manipulate this virtual address
	// convince yourself this isn't a big deal, eagle-eyes!
	pte = get_va_perms(pgdir, (void*SAFE)TC(VPT+(UVPT>>10)));
	assert((pte & PTE_U) != PTE_U);
	assert((pte & PTE_W) != PTE_W);

	cprintf("check_boot_pgdir() succeeded!\n");
}

// This function returns the physical address of the page containing 'va',
// defined by the page directory 'pgdir'.  The hardware normally performs
// this functionality for us!  We define our own version to help check
// the check_boot_pgdir() function; it shouldn't be used elsewhere.

static physaddr_t
check_va2pa(pde_t *COUNT(NPDENTRIES) _pgdir, uintptr_t va)
{
	pte_t *COUNT(NPTENTRIES) p;
	pde_t *COUNT(1) pgdir;

	pgdir = &_pgdir[PDX(va)];
	if (!(*pgdir & PTE_P))
		return ~0;
	if (*pgdir & PTE_PS)
		return PTE_ADDR(*pgdir);
	p = (pte_t*COUNT(NPTENTRIES)) KADDR(PTE_ADDR(*pgdir));
	if (!(p[PTX(va)] & PTE_P))
		return ~0;
	return PTE_ADDR(p[PTX(va)]);
}

/* 
 * Remove the second level page table associated with virtual address va.
 * Will 0 out the PDE for that page table.
 * Panics if the page table has any present entries.
 * This should be called rarely and with good cause.
 * Currently errors if the PDE is jumbo or not present.
 */
error_t	pagetable_remove(pde_t *pgdir, void *va)
{
	pde_t* the_pde = &pgdir[PDX(va)];

	if (!(*the_pde & PTE_P) || (*the_pde & PTE_PS))
		return -EFAULT;
	pte_t* page_table = (pde_t*COUNT(NPTENTRIES))KADDR(PTE_ADDR(*the_pde));
	for (int i = 0; i < NPTENTRIES; i++) 
		if (page_table[i] & PTE_P)
			panic("Page table not empty during attempted removal!");
	*the_pde = 0;
	page_decref(pa2page(PADDR(page_table)));
	return 0;
}

// Given 'pgdir', a pointer to a page directory, pgdir_walk returns
// a pointer to the page table entry (PTE) for linear address 'va'.
// This requires walking the two-level page table structure.
//
// If the relevant page table doesn't exist in the page directory, then:
//    - If create == 0, pgdir_walk returns NULL.
//    - Otherwise, pgdir_walk tries to allocate a new page table
//	with page_alloc.  If this fails, pgdir_walk returns NULL.
//    - Otherwise, pgdir_walk returns a pointer into the new page table.
//
// Hint: you can turn a Page * into the physical address of the
// page it refers to with page2pa() from kern/pmap.h.
//
// Supports returning jumbo (4MB PSE) PTEs.  To create with a jumbo, pass in 2.
pte_t*
pgdir_walk(pde_t *pgdir, const void *SNT va, int create)
{
	pde_t* the_pde = &pgdir[PDX(va)];
	page_t *new_table;

	if (*the_pde & PTE_P) {
		if (*the_pde & PTE_PS)
			return (pte_t*)the_pde;
		return &((pde_t*COUNT(NPTENTRIES))KADDR(PTE_ADDR(*the_pde)))[PTX(va)];
	}
	if (!create)
		return NULL;
	if (create == 2) {
		if (JPGOFF(va))
			panic("Attempting to find a Jumbo PTE at an unaligned VA!");
		*the_pde = PTE_PS | PTE_P;
		return (pte_t*)the_pde;
	}
	if (kpage_alloc(&new_table))
		return NULL;
	memset(page2kva(new_table), 0, PGSIZE);
	/* storing our ref to new_table in the PTE */
	*the_pde = (pde_t)page2pa(new_table) | PTE_P | PTE_W | PTE_U;
	return &((pde_t*COUNT(NPTENTRIES))KADDR(PTE_ADDR(*the_pde)))[PTX(va)];
}

/* Returns the effective permissions for PTE_U, PTE_W, and PTE_P on a given
 * virtual address.  Note we need to consider the composition of every PTE in
 * the page table walk. */
int get_va_perms(pde_t *pgdir, const void *SNT va)
{
	pde_t the_pde = pgdir[PDX(va)];
	pte_t the_pte;

	if (!(the_pde & PTE_P))
		return 0;
	if (the_pde & PTE_PS)
		return the_pde & (PTE_U | PTE_W | PTE_P);
	the_pte = ((pde_t*COUNT(NPTENTRIES))KADDR(PTE_ADDR(the_pde)))[PTX(va)];
	if (!(the_pte & PTE_P))
		return 0;
	return the_pte & the_pde & (PTE_U | PTE_W | PTE_P);
}

void
page_check(void)
{
	page_t *pp, *pp0, *pp1, *pp2;
	page_list_t fl[1024];
	pte_t *ptep;

	// should be able to allocate three pages
	pp0 = pp1 = pp2 = 0;
	assert(kpage_alloc(&pp0) == 0);
	assert(kpage_alloc(&pp1) == 0);
	assert(kpage_alloc(&pp2) == 0);

	assert(pp0);
	assert(pp1 && pp1 != pp0);
	assert(pp2 && pp2 != pp1 && pp2 != pp0);

	// temporarily steal the rest of the free pages
	for(int i=0; i<llc_cache->num_colors; i++) {
		fl[i] = colored_page_free_list[i];
		LIST_INIT(&colored_page_free_list[i]);
	}

	// should be no free memory
	assert(kpage_alloc(&pp) == -ENOMEM);

	// Fill pp1 with bogus data and check for invalid tlb entries
	memset(page2kva(pp1), 0xFFFFFFFF, PGSIZE);

	// there is no page allocated at address 0
	assert(page_lookup(boot_pgdir, (void *) 0x0, &ptep) == NULL);

	// there is no free memory, so we can't allocate a page table 
	assert(page_insert(boot_pgdir, pp1, 0x0, 0) < 0);

	// free pp0 and try again: pp0 should be used for page table
	page_decref(pp0);
	assert(page_insert(boot_pgdir, pp1, 0x0, 0) == 0);
	tlb_invalidate(boot_pgdir, 0x0);
	// DEP Should have shot down invalid TLB entry - let's check
	{ TRUSTEDBLOCK
	  int *x = 0x0;
	  assert(*x == 0xFFFFFFFF);
	}
	assert(PTE_ADDR(boot_pgdir[0]) == page2pa(pp0));
	assert(check_va2pa(boot_pgdir, 0x0) == page2pa(pp1));
	assert(kref_refcnt(&pp1->pg_kref) == 2);
	assert(kref_refcnt(&pp0->pg_kref) == 1);

	// should be able to map pp2 at PGSIZE because pp0 is already allocated for page table
	assert(page_insert(boot_pgdir, pp2, (void*SNT) PGSIZE, 0) == 0);
	assert(check_va2pa(boot_pgdir, PGSIZE) == page2pa(pp2));
	assert(kref_refcnt(&pp2->pg_kref) == 2);

	// Make sure that pgdir_walk returns a pointer to the pte and
	// not the table or some other garbage
	{
	  pte_t *p = (pte_t*COUNT(NPTENTRIES))KADDR(PTE_ADDR(boot_pgdir[PDX(PGSIZE)]));
	  assert(pgdir_walk(boot_pgdir, (void *SNT)PGSIZE, 0) == &p[PTX(PGSIZE)]);
	}

	// should be no free memory
	assert(kpage_alloc(&pp) == -ENOMEM);

	// should be able to map pp2 at PGSIZE because it's already there
	assert(page_insert(boot_pgdir, pp2, (void*SNT) PGSIZE, PTE_U) == 0);
	assert(check_va2pa(boot_pgdir, PGSIZE) == page2pa(pp2));
	assert(kref_refcnt(&pp2->pg_kref) == 2);

	// Make sure that we actually changed the permission on pp2 when we re-mapped it
	{
	  pte_t *p = pgdir_walk(boot_pgdir, (void*SNT)PGSIZE, 0);
	  assert(((*p) & PTE_U) == PTE_U);
	}

	// pp2 should NOT be on the free list
	// could happen if ref counts are handled sloppily in page_insert
	assert(kpage_alloc(&pp) == -ENOMEM);

	// should not be able to map at PTSIZE because need free page for page table
	assert(page_insert(boot_pgdir, pp0, (void*SNT) PTSIZE, 0) < 0);

	// insert pp1 at PGSIZE (replacing pp2)
	assert(page_insert(boot_pgdir, pp1, (void*SNT) PGSIZE, 0) == 0);

	// should have pp1 at both 0 and PGSIZE, pp2 nowhere, ...
	assert(check_va2pa(boot_pgdir, 0) == page2pa(pp1));
	assert(check_va2pa(boot_pgdir, PGSIZE) == page2pa(pp1));
	// ... and ref counts should reflect this
	assert(kref_refcnt(&pp1->pg_kref) == 3);
	assert(kref_refcnt(&pp2->pg_kref) == 1);

	// pp2 should be returned by page_alloc
	page_decref(pp2);	/* should free it */
	assert(kpage_alloc(&pp) == 0 && pp == pp2);

	// unmapping pp1 at 0 should keep pp1 at PGSIZE
	page_remove(boot_pgdir, 0x0);
	assert(check_va2pa(boot_pgdir, 0x0) == ~0);
	assert(check_va2pa(boot_pgdir, PGSIZE) == page2pa(pp1));
	assert(kref_refcnt(&pp1->pg_kref) == 2);
	assert(kref_refcnt(&pp2->pg_kref) == 1);

	// unmapping pp1 at PGSIZE should free it
	page_remove(boot_pgdir, (void*SNT) PGSIZE);
	assert(check_va2pa(boot_pgdir, 0x0) == ~0);
	assert(check_va2pa(boot_pgdir, PGSIZE) == ~0);
	assert(kref_refcnt(&pp1->pg_kref) == 1);
	assert(kref_refcnt(&pp2->pg_kref) == 1);
	page_decref(pp1);

	// so it should be returned by page_alloc
	assert(kpage_alloc(&pp) == 0 && pp == pp1);

	// should be no free memory
	assert(kpage_alloc(&pp) == -ENOMEM);

	// forcibly take pp0 back
	assert(PTE_ADDR(boot_pgdir[0]) == page2pa(pp0));
	boot_pgdir[0] = 0;
	assert(kref_refcnt(&pp0->pg_kref) == 1);

	// Catch invalid pointer addition in pgdir_walk - i.e. pgdir + PDX(va)
	{
	  // Give back pp0 for a bit
	  page_decref(pp0);

	  void *SNT va = (void *SNT)((PGSIZE * NPDENTRIES) + PGSIZE);
	  pte_t *p2 = pgdir_walk(boot_pgdir, va, 1);
	  pte_t *p = (pte_t*COUNT(NPTENTRIES))KADDR(PTE_ADDR(boot_pgdir[PDX(va)]));
	  assert(p2 == &p[PTX(va)]);

	  // Clean up again
	  boot_pgdir[PDX(va)] = 0;
	}

	// give free list back
	for(int i=0; i<llc_cache->num_colors; i++)
		colored_page_free_list[i] = fl[i];

	// free the pages we took
	page_decref(pp0);
	page_decref(pp1);
	page_decref(pp2);
	assert(!kref_refcnt(&pp0->pg_kref));
	assert(!kref_refcnt(&pp1->pg_kref));
	assert(!kref_refcnt(&pp2->pg_kref));

	cprintf("page_check() succeeded!\n");
}

/* Walks len bytes from start, executing 'callback' on every PTE, passing it a
 * specific VA and whatever arg is passed in.  Note, this cannot handle jumbo
 * pages. */
int env_user_mem_walk(env_t* e, void* start, size_t len,
                      mem_walk_callback_t callback, void* arg)
{
	pte_t *pt;
	uint32_t pdeno, pteno;
	physaddr_t pa;

	assert((uintptr_t)start % PGSIZE == 0 && len % PGSIZE == 0);
	uintptr_t end = (uintptr_t)start+len;
	uint32_t pdeno_start = PDX(start);
	uint32_t pdeno_end = PDX(ROUNDUP(end,PTSIZE));
	/* concerned about overflow.  this should catch it for now, given the above
	 * assert. */
	assert((len == 0) || (pdeno_start < pdeno_end));

	for (pdeno = pdeno_start; pdeno < pdeno_end; pdeno++) {
		if (!(e->env_pgdir[pdeno] & PTE_P))
			continue;
		/* find the pa and a pointer to the page table */
		pa = PTE_ADDR(e->env_pgdir[pdeno]);
		pt = (pte_t*COUNT(NPTENTRIES)) KADDR(pa);
		/* figure out where we start and end within the page table */
		uint32_t pteno_start = (pdeno == pdeno_start ? PTX(start) : 0);
		uint32_t pteno_end = (pdeno == pdeno_end - 1 && PTX(end) != 0 ?
		                      PTX(end) : NPTENTRIES );
		int ret;
		for (pteno = pteno_start; pteno < pteno_end; pteno++) {
			if((ret = callback(e, &pt[pteno], PGADDR(pdeno, pteno, 0), arg)))
				return ret;
		}
	}
	return 0;
}

/* Frees (decrefs) all pages of the process's page table, including the page
 * directory.  Does not free the memory that is actually mapped. */
void env_pagetable_free(env_t* e)
{
	static_assert(UVPT % PTSIZE == 0);
	assert(e->env_cr3 != rcr3());
	for(uint32_t pdeno = 0; pdeno < PDX(UVPT); pdeno++)
	{
		// only look at mapped page tables
		if (!(e->env_pgdir[pdeno] & PTE_P))
			continue;

		// find the pa and va of the page table
		physaddr_t pa = PTE_ADDR(e->env_pgdir[pdeno]);

		// free the page table itself
		e->env_pgdir[pdeno] = 0;
		page_decref(pa2page(pa));
	}

	// free the page directory
	physaddr_t pa = e->env_cr3;
	e->env_cr3 = 0;
	page_decref(pa2page(pa));
	tlbflush();
}
