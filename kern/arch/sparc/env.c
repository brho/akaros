/* See COPYRIGHT for copyright information. */
#ifdef __SHARC__
#pragma nosharc
#endif

#ifdef __DEPUTY__
#pragma noasync
#endif

#include <arch/trap.h>
#include <env.h>
#include <assert.h>
#include <arch/arch.h>
#include <pmap.h>

void
( env_push_ancillary_state)(env_t* e)
{
	if(e->env_tf.psr & PSR_EF)
		save_fp_state(&e->env_ancillary_state);
}

void
save_fp_state(ancillary_state_t* silly)
{
	#define push_two_fp_regs(pdest,n) \
	    __asm__ __volatile__ ("std	%%f" XSTR(n) ",[%0+4*" XSTR(n) "]" \
	                      : : "r"(pdest) : "memory");

	write_psr(read_psr() | PSR_EF);

	silly->fsr = read_fsr();

	push_two_fp_regs(silly->fpr,0);
	push_two_fp_regs(silly->fpr,2);
	push_two_fp_regs(silly->fpr,4);
	push_two_fp_regs(silly->fpr,6);
	push_two_fp_regs(silly->fpr,8);
	push_two_fp_regs(silly->fpr,10);
	push_two_fp_regs(silly->fpr,12);
	push_two_fp_regs(silly->fpr,14);
	push_two_fp_regs(silly->fpr,16);
	push_two_fp_regs(silly->fpr,18);
	push_two_fp_regs(silly->fpr,20);
	push_two_fp_regs(silly->fpr,22);
	push_two_fp_regs(silly->fpr,24);
	push_two_fp_regs(silly->fpr,26);
	push_two_fp_regs(silly->fpr,28);
	push_two_fp_regs(silly->fpr,30);

	write_psr(read_psr() & ~PSR_EF);
}

void
( env_pop_ancillary_state)(env_t* e)
{ 
	if(e->env_tf.psr & PSR_EF)
		restore_fp_state(&e->env_ancillary_state);
}

void
restore_fp_state(ancillary_state_t* silly)
{
	#define pop_two_fp_regs(pdest,n) \
	    __asm__ __volatile__ ("ldd	[%0+4*" XSTR(n) "], %%f" XSTR(n) \
	                      : : "r"(pdest) : "memory");

	write_psr(read_psr() | PSR_EF);

	pop_two_fp_regs(silly->fpr,0);
	pop_two_fp_regs(silly->fpr,2);
	pop_two_fp_regs(silly->fpr,4);
	pop_two_fp_regs(silly->fpr,6);
	pop_two_fp_regs(silly->fpr,8);
	pop_two_fp_regs(silly->fpr,10);
	pop_two_fp_regs(silly->fpr,12);
	pop_two_fp_regs(silly->fpr,14);
	pop_two_fp_regs(silly->fpr,16);
	pop_two_fp_regs(silly->fpr,18);
	pop_two_fp_regs(silly->fpr,20);
	pop_two_fp_regs(silly->fpr,22);
	pop_two_fp_regs(silly->fpr,24);
	pop_two_fp_regs(silly->fpr,26);
	pop_two_fp_regs(silly->fpr,28);
	pop_two_fp_regs(silly->fpr,30);

	write_fsr(silly->fsr);

	write_psr(read_psr() & ~PSR_EF);
}

// Flush all mapped pages in the user portion of the address space
// TODO: only supports L3 user pages
int
env_user_mem_walk(env_t* e, void* start, size_t len,
                  mem_walk_callback_t callback, void* arg)
{
	pte_t *l1pt = e->env_pgdir;

	assert((uintptr_t)start % PGSIZE == 0 && len % PGSIZE == 0);
	void* end = (char*)start+len;

	int l1x_start = L1X(start);
	int l1x_end = L1X(ROUNDUP(end,L1PGSIZE));
	for(int l1x = l1x_start; l1x < l1x_end; l1x++)
	{
		if(!(l1pt[l1x] & PTE_PTD))
			continue;

		physaddr_t l2ptpa = PTD_ADDR(l1pt[l1x]);
		pte_t* l2pt = (pte_t*)KADDR(l2ptpa);

		int l2x_start = l1x == l1x_start ? L2X(start) : 0;
		int l2x_end = l1x == l1x_end-1 && L2X(ROUNDUP(end,L2PGSIZE)) ?
		              L2X(ROUNDUP(end,L2PGSIZE)) : NL2ENTRIES;
		for(int l2x = l2x_start; l2x < l2x_end; l2x++)
		{
			if(!(l2pt[l2x] & PTE_PTD))
				continue;

			physaddr_t l3ptpa = PTD_ADDR(l2pt[l2x]);
			pte_t* l3pt = (pte_t*)KADDR(l3ptpa);

			int l3x_start = l1x == l1x_start && l2x == l2x_start ?
			                L3X(start) : 0;
			int l3x_end = l1x == l1x_end-1 && l2x == l2x_end-1 && L3X(end) ?
			              L3X(end) : NL3ENTRIES;
			for(int l3x = l3x_start, ret; l3x < l3x_end; l3x++)
				if(!PAGE_UNMAPPED(l3pt[l3x]))
					if((ret = callback(e,&l3pt[l3x],PGADDR(l1x,l2x,l3x,0),arg)))
						return ret;
		}
	}

	return 0;
}

void
env_pagetable_free(env_t* e)
{
	static_assert(L2X(KERNBASE) == 0 && L3X(KERNBASE) == 0);
	pte_t *l1pt = e->env_pgdir;

	for(int l1x = 0; l1x < L1X(KERNBASE); l1x++)
	{
		if(!(l1pt[l1x] & PTE_PTD))
			continue;

		physaddr_t l2ptpa = PTD_ADDR(l1pt[l1x]);
		pte_t* l2pt = (pte_t*)KADDR(l2ptpa);

		for(int l2x = 0; l2x < NL2ENTRIES; l2x++)
		{
			if(!(l2pt[l2x] & PTE_PTD))
				continue;

			physaddr_t l3ptpa = PTD_ADDR(l2pt[l2x]);
			l2pt[l2x] = 0;
			page_decref(pa2page(l3ptpa));
		}

		l1pt[l1x] = 0;
		page_decref(pa2page(l2ptpa));
	}

	page_decref(pa2page(e->env_cr3));
	tlbflush();
}
