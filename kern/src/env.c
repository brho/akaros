/* See COPYRIGHT for copyright information. */
#ifdef __DEPUTY__
#pragma nodeputy
#endif

#include <arch/x86.h>
#include <arch/mmu.h>
#include <arch/elf.h>
#include <arch/apic.h>
#include <arch/smp.h>

#include <error.h>
#include <string.h>
#include <assert.h>
#include <env.h>
#include <pmap.h>
#include <trap.h>
#include <monitor.h>
#include <manager.h>

#include <ros/syscall.h>

env_t *envs = NULL;		// All environments
// TODO: make this a struct of info including the pointer and cacheline-align it
// This lets the kernel know what process is running on the core it traps into.
// A lot of the Env business, including this and its usage, will change when we
// redesign the env as a multi-process.
env_t* curenvs[MAX_NUM_CPUS] = {[0 ... (MAX_NUM_CPUS-1)] NULL};
static env_list_t env_free_list;	// Free list

#define ENVGENSHIFT	12		// >= LOGNENV

//
// Converts an envid to an env pointer.
//
// RETURNS
//   0 on success, -E_BAD_ENV on error.
//   On success, sets *env_store to the environment.
//   On error, sets *env_store to NULL.
//
int
envid2env(envid_t envid, env_t **env_store, bool checkperm)
{
	env_t *e;
	env_t* curenv = curenvs[lapic_get_id()];

	// If envid is zero, return the current environment.
	if (envid == 0) {
		*env_store = curenv;
		return 0;
	}

	// Look up the Env structure via the index part of the envid,
	// then check the env_id field in that env_t
	// to ensure that the envid is not stale
	// (i.e., does not refer to a _previous_ environment
	// that used the same slot in the envs[] array).
	e = &envs[ENVX(envid)];
	if (e->env_status == ENV_FREE || e->env_id != envid) {
		*env_store = 0;
		return -E_BAD_ENV;
	}

	// Check that the calling environment has legitimate permission
	// to manipulate the specified environment.
	// If checkperm is set, the specified environment
	// must be either the current environment
	// or an immediate child of the current environment.
	if (checkperm && e != curenv && e->env_parent_id != curenv->env_id) {
		*env_store = 0;
		return -E_BAD_ENV;
	}

	*env_store = e;
	return 0;
}

//
// Mark all environments in 'envs' as free, set their env_ids to 0,
// and insert them into the env_free_list.
// Insert in reverse order, so that the first call to env_alloc()
// returns envs[0].
//
void
env_init(void)
{
	int i;
	LIST_INIT(&env_free_list);
	for (i = NENV-1; i >= 0; i--) {
		// these should already be set from when i memset'd the array to 0
		envs[i].env_status = ENV_FREE;
		envs[i].env_id = 0;
		LIST_INSERT_HEAD(&env_free_list, &envs[i], env_link);
	}
}

//
// Initialize the kernel virtual memory layout for environment e.
// Allocate a page directory, set e->env_pgdir and e->env_cr3 accordingly,
// and initialize the kernel portion of the new environment's address space.
// Do NOT (yet) map anything into the user portion
// of the environment's virtual address space.
//
// Returns 0 on success, < 0 on error.  Errors include:
//	-E_NO_MEM if page directory or table could not be allocated.
//
static int
env_setup_vm(env_t *e)
{
	int i, r;
	page_t *pgdir = NULL, *pginfo = NULL, *pgdata = NULL;

	// Allocate pages for the page directory, shared info, and shared data pages
	r = page_alloc(&pgdir);
	r = page_alloc(&pginfo);
	r = page_alloc(&pgdata);
	if (r < 0) {
		page_free(pgdir);
		page_free(pginfo);
		return r;
	}

	// Now, set e->env_pgdir and e->env_cr3,
	// and initialize the page directory.
	//
	// Hint:
	//    - The VA space of all envs is identical above UTOP
	//      (except at VPT and UVPT, which we've set below).
	//      (and not for UINFO either)
	//	See inc/memlayout.h for permissions and layout.
	//	Can you use boot_pgdir as a template?  Hint: Yes.
	//	(Make sure you got the permissions right in Lab 2.)
	//    - The initial VA below UTOP is empty.
	//    - You do not need to make any more calls to page_alloc.
	//    - Note: pp_ref is not maintained for most physical pages
	//	mapped above UTOP -- but you do need to increment
	//	env_pgdir's pp_ref!

	// need to up pgdir's reference, since it will never be done elsewhere
	pgdir->pp_ref++;
	e->env_pgdir = page2kva(pgdir);
	e->env_cr3 = page2pa(pgdir);
	e->env_procinfo = page2kva(pginfo);
	e->env_procdata = page2kva(pgdata);

	memset(e->env_pgdir, 0, PGSIZE);
	memset(e->env_procinfo, 0, PGSIZE);
	memset(e->env_procdata, 0, PGSIZE);

	// Initialize the generic syscall ring buffer
	SHARED_RING_INIT((syscall_sring_t*)e->env_procdata);
	// Initialize the backend of the ring buffer
	BACK_RING_INIT(&e->env_sysbackring, (syscall_sring_t*)e->env_procdata, PGSIZE);

	// should be able to do this so long as boot_pgdir never has
	// anything put below UTOP
	memcpy(e->env_pgdir, boot_pgdir, PGSIZE);

	// something like this.  TODO, if you want
	//memcpy(&e->env_pgdir[PDX(UTOP)], &boot_pgdir[PDX(UTOP)], PGSIZE - PDX(UTOP));
	// check with
	// assert(memcmp(e->env_pgdir, boot_pgdir, PGSIZE) == 0);

	// VPT and UVPT map the env's own page table, with
	// different permissions.
	e->env_pgdir[PDX(VPT)]  = e->env_cr3 | PTE_P | PTE_W;
	e->env_pgdir[PDX(UVPT)] = e->env_cr3 | PTE_P | PTE_U;

	// Insert the per-process info and data pages into this process's pgdir
	// I don't want to do these two pages later (like with the stack), since
	// the kernel wants to keep pointers to it easily.
	// Could place all of this with a function that maps a shared memory page
	// that can work between any two address spaces or something.
	r = page_insert(e->env_pgdir, pginfo, (void*)UINFO, PTE_U);
	r = page_insert(e->env_pgdir, pgdata, (void*)UDATA, PTE_U | PTE_W);
	if (r < 0) {
		// note that we can't currently deallocate the pages created by
		// pgdir_walk (inside insert).  should be able to gather them up when
		// we destroy environments and their page tables.
		page_free(pgdir);
		page_free(pginfo);
		page_free(pgdata);
		return r;
	}
	return 0;
}

//
// Allocates and initializes a new environment.
// On success, the new environment is stored in *newenv_store.
//
// Returns 0 on success, < 0 on failure.  Errors include:
//	-E_NO_FREE_ENV if all NENVS environments are allocated
//	-E_NO_MEM on memory exhaustion
//
int
env_alloc(env_t **newenv_store, envid_t parent_id)
{
	int32_t generation;
	int r;
	env_t *e;

	if (!(e = LIST_FIRST(&env_free_list)))
		return -E_NO_FREE_ENV;

	// Allocate and set up the page directory for this environment.
	if ((r = env_setup_vm(e)) < 0)
		return r;

	// Generate an env_id for this environment.
	generation = (e->env_id + (1 << ENVGENSHIFT)) & ~(NENV - 1);
	if (generation <= 0)	// Don't create a negative env_id.
		generation = 1 << ENVGENSHIFT;
	e->env_id = generation | (e - envs);

	// Set the basic status variables.
	e->env_parent_id = parent_id;
	e->env_status = ENV_RUNNABLE;
	e->env_runs = 0;
	e->env_refcnt = 1;

	// Clear out all the saved register state,
	// to prevent the register values
	// of a prior environment inhabiting this Env structure
	// from "leaking" into our new environment.
	memset(&e->env_tf, 0, sizeof(e->env_tf));

	// Set up appropriate initial values for the segment registers.
	// GD_UD is the user data segment selector in the GDT, and
	// GD_UT is the user text segment selector (see inc/memlayout.h).
	// The low 2 bits of each segment register contains the
	// Requestor Privilege Level (RPL); 3 means user mode.
	e->env_tf.tf_ds = GD_UD | 3;
	e->env_tf.tf_es = GD_UD | 3;
	e->env_tf.tf_ss = GD_UD | 3;
	e->env_tf.tf_esp = USTACKTOP;
	e->env_tf.tf_cs = GD_UT | 3;
	// You will set e->env_tf.tf_eip later.
	// set the env's EFLAGSs to have interrupts enabled
	e->env_tf.tf_eflags |= 0x00000200; // bit 9 is the interrupts-enabled

	// commit the allocation
	LIST_REMOVE(e, env_link);
	*newenv_store = e;

	e->env_tscfreq = system_timing.tsc_freq;
	// TODO: for now, the only info at procinfo is this env's struct
	// note that we need to copy this over every time we make a change to env
	// that we want userspace to see.  also note that we don't even want to
	// show them all of env, only specific things like PID, PPID, etc
	memcpy(e->env_procinfo, e, sizeof(env_t));

	env_t* curenv = curenvs[lapic_get_id()];

	cprintf("[%08x] new env %08x\n", curenv ? curenv->env_id : 0, e->env_id);
	return 0;
}

//
// Allocate len bytes of physical memory for environment env,
// and map it at virtual address va in the environment's address space.
// Does not zero or otherwise initialize the mapped pages in any way.
// Pages should be writable by user and kernel.
// Panic if any allocation attempt fails.
//
static void
segment_alloc(env_t *e, void *va, size_t len)
{
	void *start, *end;
	size_t num_pages;
	int i, r;
	page_t *page;
	pte_t *pte;

	start = ROUNDDOWN(va, PGSIZE);
	end = ROUNDUP(va + len, PGSIZE);
	if (start >= end)
		panic("Wrap-around in memory allocation addresses!");
	if ((uintptr_t)end > UTOP)
		panic("Attempting to map above UTOP!");
	// page_insert/pgdir_walk alloc a page and read/write to it via its address
	// starting from pgdir (e's), so we need to be using e's pgdir
	assert(e->env_cr3 == rcr3());
	num_pages = PPN(end - start);
	for (i = 0; i < num_pages; i++, start += PGSIZE) {
		// skip if a page is already mapped.  yes, page_insert will page_remove
		// whatever page was already there, but if we are seg allocing adjacent
		// regions, we don't want to destroy that old mapping/page
		// though later on we are told we can ignore this...
		pte = pgdir_walk(e->env_pgdir, start, 0);
		if (pte && *pte & PTE_P)
			continue;
		if ((r = page_alloc(&page)) < 0)
			panic("segment_alloc: %e", r);
		page_insert(e->env_pgdir, page, start, PTE_U | PTE_W);
	}
}

//
// Set up the initial program binary, stack, and processor flags
// for a user process.
// This function is ONLY called during kernel initialization,
// before running the first user-mode environment.
//
// This function loads all loadable segments from the ELF binary image
// into the environment's user memory, starting at the appropriate
// virtual addresses indicated in the ELF program header.
// At the same time it clears to zero any portions of these segments
// that are marked in the program header as being mapped
// but not actually present in the ELF file - i.e., the program's bss section.
//
// All this is very similar to what our boot loader does, except the boot
// loader also needs to read the code from disk.  Take a look at
// boot/main.c to get ideas.
//
// Finally, this function maps one page for the program's initial stack.
//
// load_icode panics if it encounters problems.
//  - How might load_icode fail?  What might be wrong with the given input?
//
static void
load_icode(env_t *e, uint8_t *binary, size_t size)
{
	// Hints:
	//  Load each program segment into virtual memory
	//  at the address specified in the ELF section header.
	//  You should only load segments with ph->p_type == ELF_PROG_LOAD.
	//  Each segment's virtual address can be found in ph->p_va
	//  and its size in memory can be found in ph->p_memsz.
	//  The ph->p_filesz bytes from the ELF binary, starting at
	//  'binary + ph->p_offset', should be copied to virtual address
	//  ph->p_va.  Any remaining memory bytes should be cleared to zero.
	//  (The ELF header should have ph->p_filesz <= ph->p_memsz.)
	//  Use functions from the previous lab to allocate and map pages.
	//
	//  All page protection bits should be user read/write for now.
	//  ELF segments are not necessarily page-aligned, but you can
	//  assume for this function that no two segments will touch
	//  the same virtual page.
	//
	//  You may find a function like segment_alloc useful.
	//
	//  Loading the segments is much simpler if you can move data
	//  directly into the virtual addresses stored in the ELF binary.
	//  So which page directory should be in force during
	//  this function?
	//
	// Hint:
	//  You must also do something with the program's entry point,
	//  to make sure that the environment starts executing there.
	//  What?  (See env_run() and env_pop_tf() below.)

	elf_t *elfhdr = (elf_t *)binary;
	int i, r;

	// is this an elf?
	assert(elfhdr->e_magic == ELF_MAGIC);
	// make sure we have proghdrs to load
	assert(elfhdr->e_phnum);

	// to actually access any pages alloc'd for this environment, we
	// need to have the hardware use this environment's page tables.
	// we can use e's tables as long as we want, since it has the same
	// mappings for the kernel as does boot_pgdir
	lcr3(e->env_cr3);

	proghdr_t *phdr = (proghdr_t *)(binary + elfhdr->e_phoff);
	for (i = 0; i < elfhdr->e_phnum; i++, phdr++) {
		if (phdr->p_type != ELF_PROG_LOAD)
			continue;
		// seg alloc creates PTE_U|PTE_W pages.  if you ever want to change
		// this, there will be issues with overlapping sections
		segment_alloc(e, (void*)phdr->p_va, phdr->p_memsz);
		memcpy((void*)phdr->p_va, binary + phdr->p_offset, phdr->p_filesz);
		memset((void*)phdr->p_va + phdr->p_filesz, 0, phdr->p_memsz - phdr->p_filesz);
	}

	e->env_tf.tf_eip = elfhdr->e_entry;

	// Now map one page for the program's initial stack
	// at virtual address USTACKTOP - PGSIZE.

	segment_alloc(e, (void*)(USTACKTOP - PGSIZE), PGSIZE);
}

//
// Allocates a new env and loads the named elf binary into it.
//
env_t* env_create(uint8_t *binary, size_t size)
{
	env_t *e;
	int r;

	if ((r = env_alloc(&e, 0)) < 0)
		panic("env_create: %e", r);
	load_icode(e, binary, size);
	return e;
}

//
// Frees env e and all memory it uses.
//
void
env_free(env_t *e)
{
	pte_t *pt;
	uint32_t pdeno, pteno;
	physaddr_t pa;

	// Note the environment's demise.
	env_t* curenv = curenvs[lapic_get_id()];
	cprintf("[%08x] free env %08x\n", curenv ? curenv->env_id : 0, e->env_id);

	// Flush all mapped pages in the user portion of the address space
	static_assert(UTOP % PTSIZE == 0);
	for (pdeno = 0; pdeno < PDX(UTOP); pdeno++) {

		// only look at mapped page tables
		if (!(e->env_pgdir[pdeno] & PTE_P))
			continue;

		// find the pa and va of the page table
		pa = PTE_ADDR(e->env_pgdir[pdeno]);
		pt = (pte_t*) KADDR(pa);

		// unmap all PTEs in this page table
		for (pteno = 0; pteno <= PTX(~0); pteno++) {
			if (pt[pteno] & PTE_P)
				page_remove(e->env_pgdir, PGADDR(pdeno, pteno, 0));
		}

		// free the page table itself
		e->env_pgdir[pdeno] = 0;
		page_decref(pa2page(pa));
	}

	// need a known good pgdir before releasing the old one
	lcr3(boot_cr3);

	// free the page directory
	pa = e->env_cr3;
	e->env_pgdir = 0;
	e->env_cr3 = 0;
	page_decref(pa2page(pa));

	// return the environment to the free list
	e->env_status = ENV_FREE;
	LIST_INSERT_HEAD(&env_free_list, e, env_link);
}

/*
 * This allows the kernel to keep this process around, in case it is being used
 * in some asynchronous processing.
 * The refcnt should always be greater than 0 for processes that aren't dying.
 * When refcnt is 0, the process is dying and should not allow any more increfs.
 * TODO: Make sure this is never called from an interrupt handler (irq_save)
 */
error_t env_incref(env_t* e)
{
	error_t retval = 0;
	spin_lock(&e->lock);
	if (e->env_refcnt)
		e->env_refcnt++;
	else
		retval = E_BAD_ENV;
	spin_unlock(&e->lock);
	return retval;
}

/*
 * When the kernel is done with a process, it decrements its reference count.
 * When the count hits 0, no one is using it and it should be freed.
 * env_destroy calls this.
 * TODO: Make sure this is never called from an interrupt handler (irq_save)
 */
void env_decref(env_t* e)
{
	spin_lock(&e->lock);
	e->env_refcnt--;
	spin_unlock(&e->lock);
	// if we hit 0, no one else will increment and we can check outside the lock
	if (e->env_refcnt == 0)
		env_free(e);
}


//
// Frees environment e.
// If e was the current env, then runs a new environment (and does not return
// to the caller).
//
void
env_destroy(env_t *e)
{
	// TODO: race condition with env statuses, esp when running / destroying
	e->env_status = ENV_DYING;
	env_decref(e);

	// for old envs that die on user cores.  since env run never returns, cores
	// never get back to their old hlt/relaxed/spin state, so we need to force
	// them back to an idle function.
	uint32_t id = lapic_get_id();
	// There is no longer a curenv for this core. (TODO: Think about this.)
	curenvs[id] = NULL;
	if (id) {
		smp_idle();
		panic("should never see me");
	}
	// else we're core 0 and can do the usual

	/* Instead of picking a new environment to run, or defaulting to the monitor
	 * like before, for now we'll hop into the manager() function, which
	 * dispatches jobs.  Note that for now we start the manager from the top,
	 * and not from where we left off the last time we called manager.  That
	 * would require us to save some context (and a stack to work on) here.
	 */
	manager();
	assert(0); // never get here

	// ugly, but for now just linearly search through all possible
	// environments for a runnable one.
	for (int i = 0; i < NENV; i++) {
		e = &envs[ENVX(i)];
		// TODO: race here, if another core is just about to start this env.
		// Fix it by setting the status in something like env_dispatch when
		// we have multi-contexted processes
		if (e && e->env_status == ENV_RUNNABLE)
			env_run(e);
	}
	cprintf("Destroyed the only environment - nothing more to do!\n");
	while (1)
		monitor(NULL);
}


//
// Restores the register values in the Trapframe with the 'iret' instruction.
// This exits the kernel and starts executing some environment's code.
// This function does not return.
//
void
env_pop_tf(trapframe_t *tf)
{
	__asm __volatile("movl %0,%%esp\n"
		"\tpopal\n"
		"\tpopl %%es\n"
		"\tpopl %%ds\n"
		"\taddl $0x8,%%esp\n" /* skip tf_trapno and tf_errcode */
		"\tiret"
		: : "g" (tf) : "memory");
	panic("iret failed");  /* mostly to placate the compiler */
}

//
// Context switch from curenv to env e.
// Note: if this is the first call to env_run, curenv is NULL.
//  (This function does not return.)
//
void
env_run(env_t *e)
{
	// Step 1: If this is a context switch (a new environment is running),
	//	   then set 'curenv' to the new environment,
	//	   update its 'env_runs' counter, and
	//	   and use lcr3() to switch to its address space.
	// Step 2: Use env_pop_tf() to restore the environment's
	//         registers and drop into user mode in the
	//         environment.

	// Hint: This function loads the new environment's state from
	//	e->env_tf.  Go back through the code you wrote above
	//	and make sure you have set the relevant parts of
	//	e->env_tf to sensible values.

	// TODO: race here with env destroy on the status and refcnt
	// Could up the refcnt and down it when a process is not running
	e->env_status = ENV_RUNNING;
	if (e != curenvs[lapic_get_id()]) {
		curenvs[lapic_get_id()] = e;
		e->env_runs++;
		lcr3(e->env_cr3);
	}
    env_pop_tf(&e->env_tf);
}

