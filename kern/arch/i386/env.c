/* See COPYRIGHT for copyright information. */
#ifdef __SHARC__
#pragma nosharc
#endif

#include <arch/trap.h>
#include <env.h>
#include <assert.h>
#include <pmap.h>

//
// This exits the kernel and starts executing some environment's code.
// This function does not return.
// Uses 'iret' or 'sysexit' depending on CS.
//
void env_pop_tf(trapframe_t *tf)
{
	/*
	 * If the process entered the kernel via sysenter, we need to leave via
	 * sysexit.  sysenter trapframes have 0 for a CS, which is pushed in
	 * sysenter_handler.
	 */
	if(tf->tf_cs) {
		/*
		 * Restores the register values in the Trapframe with the 'iret'
		 * instruction.  This exits the kernel and starts executing some
		 * environment's code.  This function does not return.
		 */
		asm volatile ("movl %0,%%esp;           "
		              "popal;                   "
		              "popl %%es;               "
		              "popl %%ds;               "
		              "addl $0x8,%%esp;         "
		              "iret                     "
		              : : "g" (tf) : "memory");
		panic("iret failed");  /* mostly to placate the compiler */
	} else {
		/* Return path of sysexit.  See sysenter_handler's asm for details. */
		asm volatile ("movl %0,%%esp;           "
		              "popal;                   "
		              "popl %%es;               "
		              "popl %%ds;               "
		              "addl $0x10, %%esp;       "
		              "popfl;                   "
		              "movl %%ebp, %%ecx;       "
		              "movl %%esi, %%edx;       "
		              "sti;                     "
		              "sysexit                  "
		              : : "g" (tf) : "memory");
		panic("sysexit failed");  /* mostly to placate the compiler */
	}
}

// Flush all mapped pages in the user portion of the address space
void
env_user_mem_free(env_t* e)
{
	pte_t *pt;
	uint32_t pdeno, pteno;
	physaddr_t pa;

	static_assert(UTOP % PTSIZE == 0);
	for (pdeno = 0; pdeno < PDX(UTOP); pdeno++) {

		// only look at mapped page tables
		if (!(e->env_pgdir[pdeno] & PTE_P))
			continue;

		// find the pa and va of the page table
		pa = PTE_ADDR(e->env_pgdir[pdeno]);
		pt = (pte_t*COUNT(NPTENTRIES)) KADDR(pa);

		// unmap all PTEs in this page table 
		for (pteno = 0; pteno <= PTX(~0); pteno++) {
			if (pt[pteno] & PTE_P)
			  	page_remove(e->env_pgdir, PGADDR(pdeno, pteno, 0));
		}

		// free the page table itself
		e->env_pgdir[pdeno] = 0;
		page_decref(pa2page(pa));
	}
}
