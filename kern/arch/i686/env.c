/* See COPYRIGHT for copyright information. */
#ifdef __SHARC__
#pragma nosharc
#endif

#include <trap.h>
#include <env.h>
#include <assert.h>
#include <pmap.h>
#include <smp.h>

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
	void* end = (char*)start+len;
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
			if (!PAGE_UNMAPPED(pt[pteno]))
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

