#ifdef __SHARC__
#pragma nosharc
#endif

#ifdef __DEPUTY__
#pragma nodeputy
#endif

#include <arch/mmu.h>
#include <ros/memlayout.h>
#include <pmap.h>
#include <string.h>
#include <kmalloc.h>

physaddr_t boot_cr3;
pde_t* boot_pgdir;
page_t* pages;
page_list_t page_free_list;

void
vm_init(void)
{
	// we already set up our page tables before jumping
	// into the kernel, so there's not much going on here

	extern pde_t l1_page_table[NL1ENTRIES];
	boot_pgdir = l1_page_table;
	boot_cr3 = PADDR(boot_pgdir);
}

error_t
pagetable_remove(pde_t* l1pt, void* va)
{
	panic("pagetable_remove doesn't work yet... -asw");
	return 0;
}

pte_t*
pgdir_walk(pde_t* l1pt, const void*SNT va, int create)
{
	pte_t *l1pte, *l2pt, *l2pte, *l3pt, *l3pte;
	page_t* new_table;

	l1pte = &l1pt[L1X(va)];
	if(*l1pte & PTE_PTE)
		return l1pte;
	if(!(*l1pte & PTE_PTD))
	{
		int i, l1x_start, l2_tables_per_page;
		physaddr_t pa;

		if(!create)
			return NULL;

		// create a new L2 PT.  we actually allocated way more
		// space than needed, so also use it for the adjacent
		// l2_tables_per_page-1 pages (if they're unmapped)

		if(kpage_alloc(&new_table))
			return NULL;
		memset(page2kva(new_table),0,PGSIZE);

		l2_tables_per_page = PGSIZE/(sizeof(pte_t)*NL2ENTRIES);
		l1x_start = L1X(va)/l2_tables_per_page*l2_tables_per_page;

		for(i = 0; i < l2_tables_per_page; i++)
		{
			if(l1pt[l1x_start+i] != 0)
				continue;

			page_incref(new_table);
			pa = page2pa(new_table) + i*sizeof(pte_t)*NL2ENTRIES;
			l1pt[l1x_start+i] = PTD(pa);
		}

		l1pte = &l1pt[L1X(va)];
	}

	l2pt = (pte_t*)KADDR(PTD_ADDR(*l1pte));
	l2pte = &l2pt[L2X(va)];
	if(*l2pte & PTE_PTE)
		return l2pte;
	if(!(*l2pte & PTE_PTD))
	{
		int i, l2x_start, l3_tables_per_page;
		physaddr_t pa;

		if(!create)
			return NULL;

		if(kpage_alloc(&new_table))
			return NULL;
		memset(page2kva(new_table),0,PGSIZE);

		l3_tables_per_page = PGSIZE/(sizeof(pte_t)*NL3ENTRIES);
		l2x_start = L2X(va)/l3_tables_per_page*l3_tables_per_page;

		for(i = 0; i < l3_tables_per_page; i++)
		{
			if(l2pt[l2x_start+i] != 0)
				continue;

			page_incref(new_table);
			pa = page2pa(new_table) + i*sizeof(pte_t)*NL3ENTRIES;
			l2pt[l2x_start+i] = PTD(pa);
		}

		l2pte = &l2pt[L2X(va)];
	}

	l3pt = (pte_t*)KADDR(PTD_ADDR(*l2pte));
	l3pte = &l3pt[L3X(va)];
	return l3pte;
}

/* TODO: this is probably wrong, since it only returns the pte as if it were the
 * perms. */
int get_va_perms(pde_t *pgdir, const void *SNT va)
{
	pte_t* pte = pgdir_walk(pgdir, va, 0);
	return pte == NULL ? 0 : (*pte & (PTE_ACC | PTE_PTE));
}

void
page_check(void)
{
}

void* mmio_alloc(physaddr_t pa, size_t size) {

	return NULL;

}
