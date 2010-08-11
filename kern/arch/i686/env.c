/* See COPYRIGHT for copyright information. */
#ifdef __SHARC__
#pragma nosharc
#endif

#include <arch/trap.h>
#include <env.h>
#include <assert.h>
#include <pmap.h>
#include <smp.h>

//
// This exits the kernel and starts executing some environment's code.
// This function does not return.
// Uses 'iret' or 'sysexit' depending on CS.
//
void env_pop_tf(trapframe_t *tf)
{
	/* Bug with this whole idea (TODO: (TLSV))*/
	/* Load the LDT for this process.  Slightly ghetto doing it here. */
	/* copy-in and check the LDT location.  the segmentation hardware write the
	 * accessed bit, so we want the memory to be in the user-writeable area. */
	segdesc_t *ldt = current->procdata->ldt;
	ldt = (segdesc_t*)MIN((uintptr_t)ldt, UTOP - LDT_SIZE);
	/* Only set up the ldt if a pointer to the ldt actually exists */
	if(ldt != NULL) {
		segdesc_t *my_gdt = per_cpu_info[core_id()].gdt;
		segdesc_t ldt_temp = SEG_SYS(STS_LDT, (uint32_t)ldt, LDT_SIZE, 3);
		my_gdt[GD_LDT >> 3] = ldt_temp;
		asm volatile("lldt %%ax" :: "a"(GD_LDT));
	}

	/* In case they are enabled elsewhere.  We can't take an interrupt in these
	 * routines, due to how they play with the kernel stack pointer. */
	disable_irq();
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
		              "popl %%gs;               "
		              "popl %%fs;               "
		              "popl %%es;               "
		              "popl %%ds;               "
		              "addl $0x8,%%esp;         "
		              "iret                     "
		              : : "g" (tf) : "memory");
		panic("iret failed");  /* mostly to placate the compiler */
	} else {
		/* Return path of sysexit.  See sysenter_handler's asm for details.
		 * One difference is that this tf could be somewhere other than a stack
		 * (like in a struct proc).  We need to make sure esp is valid once
		 * interrupts are turned on (which would happen on popfl normally), so
		 * we need to save and restore a decent esp (the current one).  We need
		 * a place to save it that is accessible after we change the stack
		 * pointer to the tf *and* that is specific to this core/instance of
		 * sysexit.  The simplest and nicest is to use the tf_esp, which we
		 * can just pop.  Incidentally, the value in oesp would work too.
		 * To prevent popfl from turning interrupts on, we hack the tf's eflags
		 * so that we have a chance to change esp to a good value before
		 * interrupts are enabled.  The other option would be to throw away the
		 * eflags, but that's less desirable. */
		tf->tf_eflags &= !FL_IF;
		tf->tf_esp = read_esp();
		asm volatile ("movl %0,%%esp;           "
		              "popal;                   "
		              "popl %%gs;               "
		              "popl %%fs;               "
		              "popl %%es;               "
		              "popl %%ds;               "
		              "addl $0x10,%%esp;        "
		              "popfl;                   "
		              "movl %%ebp,%%ecx;        "
		              "popl %%esp;              "
		              "sti;                     "
		              "sysexit                  "
		              : : "g" (tf) : "memory");
		panic("sysexit failed");  /* mostly to placate your mom */
	}
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

