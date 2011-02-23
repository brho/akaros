/* See COPYRIGHT for copyright information. */

#ifdef __SHARC__
#pragma nosharc
#endif

#include <arch/arch.h>
#include <arch/mmu.h>
#include <bitmask.h>
#include <elf.h>
#include <smp.h>
#include <atomic.h>
#include <string.h>
#include <assert.h>
#include <process.h>
#include <pmap.h>
#include <trap.h>
#include <monitor.h>
#include <manager.h>
#include <stdio.h>
#include <schedule.h>
#include <kmalloc.h>
#include <mm.h>

#include <ros/syscall.h>
#include <error.h>

atomic_t num_envs;

// Initialize the kernel virtual memory layout for environment e.
// Allocate a page directory, set e->env_pgdir and e->env_cr3 accordingly,
// and initialize the kernel portion of the new environment's address space.
// Do NOT (yet) map anything into the user portion
// of the environment's virtual address space.
//
// Returns 0 on success, < 0 on error.  Errors include:
//	-ENOMEM if page directory or table could not be allocated.
//
int env_setup_vm(env_t *e)
WRITES(e->env_pgdir, e->env_cr3, e->procinfo, e->procdata)
{
	int i, r;
	page_t *pgdir = NULL;
	static page_t * RO shared_page = 0;

	/* Get a page for the pgdir.  Storing the ref in pgdir/env_pgdir */
	r = kpage_alloc(&pgdir);
	if (r < 0)
		return r;

	/*
	 * Next, set up the e->env_pgdir and e->env_cr3 pointers to point
	 * to this newly allocated page and clear its contents
	 */
	memset(page2kva(pgdir), 0, PGSIZE);
	e->env_pgdir = (pde_t *COUNT(NPDENTRIES)) TC(page2kva(pgdir));
	e->env_cr3 =   (physaddr_t) TC(page2pa(pgdir));

	/*
	 * Now start filling in the pgdir with mappings required by all newly
	 * created address spaces
	 */

	// Map in the kernel to the top of every address space
	// should be able to do this so long as boot_pgdir never has
	// anything put below UTOP
	// TODO check on this!  had a nasty bug because of it
	// this is a bit wonky, since if it's not PGSIZE, lots of other things are
	// screwed up...
	memcpy(e->env_pgdir, boot_pgdir, NPDENTRIES*sizeof(pde_t));

	// VPT and UVPT map the env's own page table, with
	// different permissions.
	e->env_pgdir[PDX(VPT)]  = PTE(LA2PPN(e->env_cr3), PTE_P | PTE_KERN_RW);
	e->env_pgdir[PDX(UVPT)] = PTE(LA2PPN(e->env_cr3), PTE_P | PTE_USER_RO);

	/* These need to be contiguous, so the kernel can alias them.  Note the
	 * pages return with a refcnt, but it's okay to insert them since we free
	 * them manually when the process is cleaned up. */
	if (!(e->procinfo = get_cont_pages(LOG2_UP(PROCINFO_NUM_PAGES), 0)))
		goto env_setup_vm_error_i;
	if (!(e->procdata = get_cont_pages(LOG2_UP(PROCDATA_NUM_PAGES), 0)))
		goto env_setup_vm_error_d;
	for (int i = 0; i < PROCINFO_NUM_PAGES; i++) {
		if (page_insert(e->env_pgdir, kva2page((void*)e->procinfo + i *
		                PGSIZE), (void*SNT)(UINFO + i*PGSIZE), PTE_USER_RO) < 0)
			goto env_setup_vm_error;
	}
	for (int i = 0; i < PROCDATA_NUM_PAGES; i++) {
		if (page_insert(e->env_pgdir, kva2page((void*)e->procdata + i *
		                PGSIZE), (void*SNT)(UDATA + i*PGSIZE), PTE_USER_RW) < 0)
			goto env_setup_vm_error;
	}
	memset(e->procinfo, 0, sizeof(struct procinfo));
	memset(e->procdata, 0, sizeof(struct procdata));

	/* Finally, set up the Global Shared Data page for all processes.  Can't be
	 * trusted, but still very useful at this stage for us.  Consider removing
	 * when we have real processes (TODO). 
	 *
	 * Note the page is alloced only the first time through, and its ref is
	 * stored in shared_page. */
	if (!shared_page) {
		if (upage_alloc(e, &shared_page, 1) < 0)
			goto env_setup_vm_error;
	}
	if (page_insert(e->env_pgdir, shared_page, (void*)UGDATA, PTE_USER_RW) < 0)
		goto env_setup_vm_error;

	return 0;

env_setup_vm_error:
	free_cont_pages(e->procdata, LOG2_UP(PROCDATA_NUM_PAGES));
env_setup_vm_error_d:
	free_cont_pages(e->procinfo, LOG2_UP(PROCINFO_NUM_PAGES));
env_setup_vm_error_i:
	page_decref(shared_page);
	env_user_mem_free(e, 0, UVPT);
	env_pagetable_free(e);
	return -ENOMEM;
}

#define PER_CPU_THING(type,name)\
type SLOCKED(name##_lock) * RWPROTECT name;\
type SLOCKED(name##_lock) *\
(get_per_cpu_##name)()\
{\
	{ R_PERMITTED(global(name))\
		return &name[core_id()];\
	}\
}

/* Frees (decrefs) all memory mapped in the given range */
void env_user_mem_free(env_t* e, void* start, size_t len)
{
	assert((uintptr_t)start + len <= UVPT); //since this keeps fucking happening
	int user_page_free(env_t* e, pte_t* pte, void* va, void* arg)
	{
		if(PAGE_PRESENT(*pte))
		{
			page_t* page = ppn2page(PTE2PPN(*pte));
			*pte = 0;
			page_decref(page);
		} else {
			assert(PAGE_PAGED_OUT(*pte));
			/* TODO: (SWAP) deal with this */
			panic("Swapping not supported!");
			*pte = 0;
		}
		return 0;
	}

	env_user_mem_walk(e,start,len,&user_page_free,NULL);
	tlbflush();
}

