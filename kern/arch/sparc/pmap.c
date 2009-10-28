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

	size_t env_array_size = ROUNDUP(NENV*sizeof(env_t), PGSIZE);
	envs = /*(env_t *)*/boot_calloc(env_array_size, 1, PGSIZE);
	//memset(envs, 0, env_array_size);
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

void *get_free_va_range(pde_t *pgdir, uintptr_t addr, size_t len)
{
	// SARAH TODO
	assert(0);
}




void
page_check(void)
{
/*
	page_t *pp, *pp0, *pp1, *pp2;
	page_list_t fl;
	pte_t *ptep;

	// should be able to allocate three pages
	pp0 = pp1 = pp2 = 0;
	assert(page_alloc(&pp0) == 0);
	assert(page_alloc(&pp1) == 0);
	assert(page_alloc(&pp2) == 0);

	assert(pp0);
	assert(pp1 && pp1 != pp0);
	assert(pp2 && pp2 != pp1 && pp2 != pp0);

	// temporarily steal the rest of the free pages
	fl = page_free_list;
	LIST_INIT(&page_free_list);

	// should be no free memory
	assert(page_alloc(&pp) == -ENOMEM);

	// Fill pp1 with bogus data and check for invalid tlb entries
	memset(page2kva(pp1), 0xFFFFFFFF, PGSIZE);

	// there is no page allocated at address 0
	assert(page_lookup(boot_pgdir, (void *) 0x0, &ptep) == NULL);

	// there is no free memory, so we can't allocate a page table 
	assert(page_insert(boot_pgdir, pp1, 0x0, 0) < 0);

	// free pp0 and try again: pp0 should be used for page table
	page_free(pp0);
	assert(page_insert(boot_pgdir, pp1, 0x0, 0) == 0);
	tlb_invalidate(boot_pgdir, 0x0);
	// DEP Should have shot down invalid TLB entry - let's check
	{
	  int *x = 0x0;
	  assert(*x == 0xFFFFFFFF);
	}
	assert(PTD_ADDR(boot_pgdir[0]) == page2pa(pp0));
	assert(check_va2pa(boot_pgdir, 0x0) == page2pa(pp1));
	assert(pp1->page_ref == 1);
	assert(pp0->page_ref == 1);

	// should be able to map pp2 at PGSIZE because pp0 is already allocated for page table
	assert(page_insert(boot_pgdir, pp2, (void*) PGSIZE, 0) == 0);
	assert(check_va2pa(boot_pgdir, PGSIZE) == page2pa(pp2));
	assert(pp2->page_ref == 1);

	// Make sure that pgdir_walk returns a pointer to the pte and
	// not the table or some other garbage
	{
	  pte_t *p = KADDR(PTD_ADDR(boot_pgdir[PDX(PGSIZE)]));
	  assert(pgdir_walk(boot_pgdir, (void *)PGSIZE, 0) == &p[PTX(PGSIZE)]);
	}

	// should be no free memory
	assert(page_alloc(&pp) == -ENOMEM);

	// should be able to map pp2 at PGSIZE because it's already there
	assert(page_insert(boot_pgdir, pp2, (void*) PGSIZE, PTE_U) == 0);
	assert(check_va2pa(boot_pgdir, PGSIZE) == page2pa(pp2));
	assert(pp2->page_ref == 1);

	// Make sure that we actually changed the permission on pp2 when we re-mapped it
	{
	  pte_t *p = pgdir_walk(boot_pgdir, (void*)PGSIZE, 0);
	  assert(((*p) & PTE_U) == PTE_U);
	}

	// pp2 should NOT be on the free list
	// could happen in ref counts are handled sloppily in page_insert
	assert(page_alloc(&pp) == -ENOMEM);

	// should not be able to map at PTSIZE because need free page for page table
	assert(page_insert(boot_pgdir, pp0, (void*) PTSIZE, 0) < 0);

	// insert pp1 at PGSIZE (replacing pp2)
	assert(page_insert(boot_pgdir, pp1, (void*) PGSIZE, 0) == 0);

	// should have pp1 at both 0 and PGSIZE, pp2 nowhere, ...
	assert(check_va2pa(boot_pgdir, 0) == page2pa(pp1));
	assert(check_va2pa(boot_pgdir, PGSIZE) == page2pa(pp1));
	// ... and ref counts should reflect this
	assert(pp1->page_ref == 2);
	assert(pp2->page_ref == 0);

	// pp2 should be returned by page_alloc
	assert(page_alloc(&pp) == 0 && pp == pp2);

	// unmapping pp1 at 0 should keep pp1 at PGSIZE
	page_remove(boot_pgdir, 0x0);
	assert(check_va2pa(boot_pgdir, 0x0) == ~0);
	assert(check_va2pa(boot_pgdir, PGSIZE) == page2pa(pp1));
	assert(pp1->page_ref == 1);
	assert(pp2->page_ref == 0);

	// unmapping pp1 at PGSIZE should free it
	page_remove(boot_pgdir, (void*) PGSIZE);
	assert(check_va2pa(boot_pgdir, 0x0) == ~0);
	assert(check_va2pa(boot_pgdir, PGSIZE) == ~0);
	assert(pp1->page_ref == 0);
	assert(pp2->page_ref == 0);

	// so it should be returned by page_alloc
	assert(page_alloc(&pp) == 0 && pp == pp1);

	// should be no free memory
	assert(page_alloc(&pp) == -ENOMEM);

	// forcibly take pp0 back
	assert(PTD_ADDR(boot_pgdir[0]) == page2pa(pp0));
	boot_pgdir[0] = 0;
	assert(pp0->page_ref == 1);
	pp0->page_ref = 0;

	// Catch invalid pointer addition in pgdir_walk - i.e. pgdir + PDX(va)
	{
	  // Give back pp0 for a bit
	  page_free(pp0);

	  void * va = (void *)((PGSIZE * NPDENTRIES) + PGSIZE);
	  pte_t *p2 = pgdir_walk(boot_pgdir, va, 1);
	  pte_t *p = KADDR(PTD_ADDR(boot_pgdir[PDX(va)]));
	  assert(p2 == &p[PTX(va)]);

	  // Clean up again
	  boot_pgdir[PDX(va)] = 0;
	  pp0->page_ref = 0;
	}

	// give free list back
	page_free_list = fl;

	// free the pages we took
	page_free(pp0);
	page_free(pp1);
	page_free(pp2);

	cprintf("page_check() succeeded!\n");
*/
}
