/* See COPYRIGHT for copyright information. */

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
{
	int i, ret;
	static page_t *shared_page = 0;

	if ((ret = arch_pgdir_setup(boot_pgdir, &e->env_pgdir)))
		return ret;
	e->env_cr3 = arch_pgdir_get_cr3(e->env_pgdir);

	/* These need to be contiguous, so the kernel can alias them.  Note the
	 * pages return with a refcnt, but it's okay to insert them since we free
	 * them manually when the process is cleaned up. */
	if (!(e->procinfo = kpages_alloc(PROCINFO_NUM_PAGES * PGSIZE, MEM_WAIT)))
		goto env_setup_vm_error_i;
	if (!(e->procdata = kpages_alloc(PROCDATA_NUM_PAGES * PGSIZE, MEM_WAIT)))
		goto env_setup_vm_error_d;
	/* Normally we would 0 the pages here.  We handle it in proc_init_proc*.
	 * Do not start the process without calling those. */
	for (int i = 0; i < PROCINFO_NUM_PAGES; i++) {
		if (page_insert(e->env_pgdir, kva2page((void*)e->procinfo + i *
		                PGSIZE), (void*)(UINFO + i*PGSIZE), PTE_USER_RO) < 0)
			goto env_setup_vm_error;
	}
	for (int i = 0; i < PROCDATA_NUM_PAGES; i++) {
		if (page_insert(e->env_pgdir, kva2page((void*)e->procdata + i *
		                PGSIZE), (void*)(UDATA + i*PGSIZE), PTE_USER_RW) < 0)
			goto env_setup_vm_error;
	}
	for (int i = 0; i < PROCGINFO_NUM_PAGES; i++) {
		if (page_insert(e->env_pgdir,
		                kva2page((void*)&__proc_global_info + i * PGSIZE),
		                (void*)(UGINFO + i * PGSIZE), PTE_USER_RO) < 0)
			goto env_setup_vm_error;
	}
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
	kpages_free(e->procdata, PROCDATA_NUM_PAGES * PGSIZE);
env_setup_vm_error_d:
	kpages_free(e->procinfo, PROCINFO_NUM_PAGES * PGSIZE);
env_setup_vm_error_i:
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
	int user_page_free(env_t* e, pte_t pte, void* va, void* arg)
	{
		if (!pte_is_mapped(pte))
			return 0;
		page_t *page = pa2page(pte_get_paddr(pte));
		pte_clear(pte);
		page_decref(page);
		/* TODO: consider other states here (like !P, yet still tracking a page,
		 * for VM tricks, page map stuff, etc.  Should be okay: once we're
		 * freeing, everything else about this proc is dead. */
		return 0;
	}

	env_user_mem_walk(e,start,len,&user_page_free,NULL);
	tlbflush();
}

void set_username(struct username *u, char *name)
{
	ERRSTACK(1);

	spin_lock(&u->name_lock);

	if (waserror()) {
		spin_unlock(&u->name_lock);
		nexterror();
	}

	__set_username(u, name);

	poperror();
	spin_unlock(&u->name_lock);
}

/*
 * This function exists so that you can do your own locking - do not use it
 * without locking the username's spinlock yourself.
 */
void __set_username(struct username *u, char *name)
{
	if (!name)
		error(EINVAL, "New username is NULL");

	if (strlen(name) > sizeof(u->name) - 1)
		error(EINVAL, "New username for process more than %d chars long",
		      sizeof(u->name) - 1);

	// 'backward' copy since reads aren't protected
	u->name[0] = 0;
	wmb(); // ensure user.name="" before writing the rest of the new name
	strlcpy(&u->name[1], &name[1], sizeof(u->name));
	wmb(); // ensure new name is written before writing first byte
	u->name[0] = name[0];
}
