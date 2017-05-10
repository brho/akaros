/* See COPYRIGHT for copyright information. */
#include <arch/arch.h>
#include <arch/mmu.h>

#include <error.h>
#include <sys/queue.h>

#include <atomic.h>
#include <string.h>
#include <assert.h>
#include <pmap.h>
#include <env.h>
#include <stdio.h>
#include <kmalloc.h>
#include <page_alloc.h>

#warning "convert pgdir* to pgdir_t"
pgdir_t* boot_pgdir;		// Virtual address of boot time page directory
physaddr_t boot_cr3;		// Physical address of boot time page directory

// --------------------------------------------------------------
// Set up initial memory mappings and turn on MMU.
// --------------------------------------------------------------

void
vm_init(void)
{
	// we already set up our page tables before jumping
	// into the kernel, so there's not much going on here

	extern pte_t l1pt[NPTENTRIES];
	boot_pgdir = l1pt;
	boot_cr3 = PADDR(boot_pgdir);
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
// This is boot_pgdir_walk, but using page_alloc() instead of boot_alloc().
// Unlike boot_pgdir_walk, pgdir_walk can fail.
pte_t*
pgdir_walk(pgdir_t *pgdir, const void *va, int create)
{
	pte_t* ppte;
	pte_t* pt;

	pt = pgdir;
	for(int i = 0; i < NPTLEVELS-1; i++)
	{
		// this code relies upon the fact that all page tables are the same size
		uintptr_t idx = (uintptr_t)va >> (L1PGSHIFT - i*(L1PGSHIFT-L2PGSHIFT));
		idx = idx & (NPTENTRIES-1);

		ppte = &pt[idx];

		if(*ppte & PTE_E)
			return ppte;

		if(!(*ppte & PTE_T))
		{
			if(!create)
				return NULL;

			page_t *new_table;
			if(kpage_alloc(&new_table))
				return NULL;
			memset(page2kva(new_table), 0, PGSIZE);

			*ppte = PTD(page2pa(new_table));
		}

		pt = (pte_t*)KADDR(PTD_ADDR(*ppte));
	}

	uintptr_t idx = (uintptr_t)va >> (L1PGSHIFT - (NPTLEVELS-1)*(L1PGSHIFT-L2PGSHIFT));
	idx = idx & (NPTENTRIES-1);
  return &pt[idx];
}

/* Returns the effective permissions for PTE_U, PTE_W, and PTE_P on a given
 * virtual address. */
int get_va_perms(pgdir_t *pgdir, const void *va)
{
	pte_t* pte = pgdir_walk(pgdir, va, 0);
	return pte == NULL ? 0 : (*pte & (PTE_PERM | PTE_E));
}

void
page_check(void)
{
}

uintptr_t gva2gpa(struct proc *p, uintptr_t cr3, uintptr_t gva)
{
	panic("Unimplemented");
	return 0;
}

int arch_pgdir_setup(pgdir_t boot_copy, pgdir_t *new_pd)
{
	pte_t *kpt = kpage_alloc_addr();
	if (!kpt)
		return -ENOMEM;
	memcpy(kpt, (pte_t*)boot_copy, PGSIZE);

	/* TODO: VPT/UVPT mappings */

	*new_pd = (pgdir_t)kpt;
	return 0;
}

physaddr_t arch_pgdir_get_cr3(pgdir_t pd)
{
	return PADDR((pte_t*)pd);
}

void arch_pgdir_clear(pgdir_t *pd)
{
	*pd = 0;
}

/* Returns the page shift of the largest jumbo supported */
int arch_max_jumbo_page_shift(void)
{
	#warning "What jumbo page sizes does RISC support?"
	return PGSHIFT;
}

#warning "Not sure where you do your PT destruction.  Be sure to not unmap any intermediate page tables for kernel mappings.  At least not the PML(n-1) maps"

void arch_add_intermediate_pts(pgdir_t pgdir, uintptr_t va, size_t len)
{
	#error "Implement me"
}

void map_segment(pgdir_t pgdir, uintptr_t va, size_t size, physaddr_t pa,
                 int perm, int pml_shift)
{
	#error "Implement me"
}

int unmap_segment(pgdir_t pgdir, uintptr_t va, size_t size)
{
	#error "Implement me"
}
