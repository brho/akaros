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
		              "popl %%es;               "
		              "popl %%ds;               "
		              "addl $0x10,%%esp;        "
		              "popfl;                   "
		              "movl %%ebp,%%ecx;        "
		              "movl %%esi,%%edx;        "
		              "popl %%esp;              "
		              "sti;                     "
		              "sysexit                  "
		              : : "g" (tf) : "memory");
		panic("sysexit failed");  /* mostly to placate your mom */
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
