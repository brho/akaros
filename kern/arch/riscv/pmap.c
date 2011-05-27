/* See COPYRIGHT for copyright information. */
#include <arch/arch.h>
#include <arch/mmu.h>

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

pde_t* boot_pgdir;		// Virtual address of boot time page directory
physaddr_t boot_cr3;		// Physical address of boot time page directory
page_t* pages = NULL;          // Virtual address of physical page array

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
pgdir_walk(pde_t *pgdir, const void *SNT va, int create)
{
  pte_t* ppte[NPTLEVELS];
	pte_t* pt[NPTLEVELS];

	pt[0] = pgdir;
	for(int i = 0; i < NPTLEVELS-1; i++)
	{
	  // this code relies upon the fact that all page tables are the same size
	  uintptr_t idx = (uintptr_t)va >> (L1PGSHIFT - i*(L1PGSHIFT-L2PGSHIFT));
		idx = idx & (NPTENTRIES-1);

		ppte[i] = &pt[i][idx];

		if(*ppte[i] & PTE_E)
			return ppte[i];

  	if(!(*ppte[i] & PTE_T))
		{
			if(!create)
				return NULL;

			page_t *new_table;
			if(kpage_alloc(&new_table))
				return NULL;
			memset(page2kva(new_table), 0, PGSIZE);

			*ppte[i] = PTD(page2pa(new_table));
		}

		pt[i+1] = (pte_t*)KADDR(PTD_ADDR(*ppte[i]));
	}

	uintptr_t idx = (uintptr_t)va >> (L1PGSHIFT - (NPTLEVELS-1)*(L1PGSHIFT-L2PGSHIFT));
	idx = idx & (NPTENTRIES-1);
  return &pt[NPTLEVELS-1][idx];
}

/* Returns the effective permissions for PTE_U, PTE_W, and PTE_P on a given
 * virtual address. */
int get_va_perms(pde_t *pgdir, const void *SNT va)
{
	pte_t* pte = pgdir_walk(pgdir, va, 0);
	return pte == NULL ? 0 : (*pte & (PTE_PERM | PTE_E));
}

void
page_check(void)
{
}

void* mmio_alloc(physaddr_t pa, size_t size)
{
	return NULL;
}
