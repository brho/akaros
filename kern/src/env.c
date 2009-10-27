/* See COPYRIGHT for copyright information. */

#ifdef __SHARC__
#pragma nosharc
#endif

#include <arch/arch.h>
#include <arch/mmu.h>
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

#include <ros/syscall.h>
#include <ros/error.h>

env_t *envs = NULL;		// All environments
atomic_t num_envs;
// TODO: make this a struct of info including the pointer and cacheline-align it
// This lets the kernel know what process is running on the core it traps into.
// A lot of the Env business, including this and its usage, will change when we
// redesign the env as a multi-process.
env_t* (RO curenvs)[MAX_NUM_CPUS] = {[0 ... (MAX_NUM_CPUS-1)] NULL};

#define ENVGENSHIFT	12		// >= LOGNENV

//
// Converts an envid to an env pointer.
//
// RETURNS
//   0 on success, -EBADENV on error.
//   On success, sets *env_store to the environment.
//   On error, sets *env_store to NULL.
//
int
envid2env(envid_t envid, env_t **env_store, bool checkperm)
{
	env_t *e;

	// If envid is zero, return the current environment.
	if (envid == 0) {
		*env_store = current;
		return 0;
	}

	// Look up the Env structure via the index part of the envid,
	// then check the env_id field in that env_t
	// to ensure that the envid is not stale
	// (i.e., does not refer to a _previous_ environment
	// that used the same slot in the envs[] array).
	e = &envs[ENVX(envid)];
	if (e->state == ENV_FREE || e->env_id != envid) {
		*env_store = 0;
		return -EBADENV;
	}

	// Check that the calling environment has legitimate permission
	// to manipulate the specified environment.
	// If checkperm is set, the specified environment
	// must be either the current environment
	// or an immediate child of the current environment.
	// TODO: should check for current being null
	if (checkperm && e != current && e->env_parent_id != current->env_id) {
		*env_store = 0;
		return -EBADENV;
	}

	*env_store = e;
	return 0;
}

//
// Mark all environments in 'envs' as free, set their env_ids to 0,
// and insert them into the proc_freelist.
// Insert in reverse order, so that the first call to env_alloc()
// returns envs[0].
// TODO: get rid of this whole array bullshit
//
void
env_init(void)
{
	int i;

	schedule_init();
	// core 0 is not idle, all others are (for now)
	spin_lock(&idle_lock);
	num_idlecores = num_cpus - 1;
	for (i = 0; i < num_idlecores; i++)
		idlecoremap[i] = i + 1;
	spin_unlock(&idle_lock);
	atomic_init(&num_envs, 0);
	TAILQ_INIT(&proc_freelist);
	assert(envs != NULL);
	for (i = NENV-1; i >= 0; i--) {
		// these should already be set from when i memset'd the array to 0
		envs[i].state = ENV_FREE;
		envs[i].env_id = 0;
		TAILQ_INSERT_HEAD(&proc_freelist, &envs[i], proc_link);
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
//	-ENOMEM if page directory or table could not be allocated.
//
static int
env_setup_vm(env_t *e)
WRITES(e->env_pgdir, e->env_cr3, e->env_procinfo, e->env_procdata)
{
	int i, r;
	page_t *pgdir = NULL;
	page_t *pginfo[PROCINFO_NUM_PAGES] = {NULL};
	page_t *pgdata[PROCDATA_NUM_PAGES] = {NULL};
	static page_t * RO shared_page = 0;

	/*
	 * First, allocate a page for the pgdir of this process and up
	 * its reference count since this will never be done elsewhere
	 */
	r = page_alloc(&pgdir);
	if(r < 0) return r;
	page_incref(pgdir);

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
	e->env_pgdir[PDX(VPT)]  = PTE(PPN(e->env_cr3), PTE_P | PTE_KERN_RW);
	e->env_pgdir[PDX(UVPT)] = PTE(PPN(e->env_cr3), PTE_P | PTE_USER_RO);

	/*
	 * Now allocate and insert all pages required for the shared
	 * procinfo structure into the page table
	 */
	for(int i=0; i<PROCINFO_NUM_PAGES; i++) {
		if(page_alloc(&pginfo[i]) < 0)
			goto env_setup_vm_error;
		if(page_insert(e->env_pgdir, pginfo[i], (void*SNT)(UINFO + i*PGSIZE),
		               PTE_USER_RO) < 0)
			goto env_setup_vm_error;
	}

	/*
	 * Now allocate and insert all pages required for the shared
	 * procdata structure into the page table
	 */
	for(int i=0; i<PROCDATA_NUM_PAGES; i++) {
		if(page_alloc(&pgdata[i]) < 0)
			goto env_setup_vm_error;
		if(page_insert(e->env_pgdir, pgdata[i], (void*SNT)(UDATA + i*PGSIZE),
		               PTE_USER_RW) < 0)
			goto env_setup_vm_error;
	}

	/*
	 * Now, set e->env_procinfo, and e->env_procdata to point to
	 * the proper pages just allocated and clear them out.
	 */
	e->env_procinfo = (procinfo_t *SAFE) TC(page2kva(pginfo[0]));
	e->env_procdata = (procdata_t *SAFE) TC(page2kva(pgdata[0]));

	memset(e->env_procinfo, 0, sizeof(procinfo_t));
	memset(e->env_procdata, 0, sizeof(procdata_t));

	/* Finally, set up the Global Shared Data page for all processes.
	 * Can't be trusted, but still very useful at this stage for us.
	 * Consider removing when we have real processes.
	 * (TODO).  Note the page is alloced only the first time through
	 */
	if (!shared_page) {
		if(page_alloc(&shared_page) < 0)
			goto env_setup_vm_error;
		// Up it, so it never goes away.  One per user, plus one from page_alloc
		// This is necessary, since it's in the per-process range of memory that
		// gets freed during page_free.
		page_incref(shared_page);
	}

	// Inserted into every process's address space at UGDATA
	if(page_insert(e->env_pgdir, shared_page, (void*SNT)UGDATA, PTE_USER_RW) < 0)
		goto env_setup_vm_error;

	return 0;

env_setup_vm_error:
	page_free(shared_page);
	for(int i=0; i< PROCDATA_NUM_PAGES; i++) {
		page_free(pgdata[i]);
	}
	for(int i=0; i< PROCINFO_NUM_PAGES; i++) {
		page_free(pginfo[i]);
	}
	env_user_mem_free(e);
	page_free(pgdir);
	return -ENOMEM;
}

//
// Allocates and initializes a new environment.
// On success, the new environment is stored in *newenv_store.
//
// Returns 0 on success, < 0 on failure.  Errors include:
//	-ENOFREEENV if all NENVS environments are allocated
//	-ENOMEM on memory exhaustion
//
int
env_alloc(env_t **newenv_store, envid_t parent_id)
{
	int32_t generation;
	int r;
	env_t *e;

	spin_lock(&freelist_lock);
	e = TAILQ_FIRST(&proc_freelist);
	if (e) {
		TAILQ_REMOVE(&proc_freelist, e, proc_link);
		spin_unlock(&freelist_lock);
	} else {
		spin_unlock(&freelist_lock);
		return -ENOFREEENV;
	}

    { INITSTRUCT(*e)

	// Allocate and set up the page directory for this environment.
	if ((r = env_setup_vm(e)) < 0) {
		spin_lock(&freelist_lock);
		TAILQ_INSERT_HEAD(&proc_freelist, e, proc_link);
		spin_unlock(&freelist_lock);
		return r;
	}

	// Generate an env_id for this environment.
	generation = (e->env_id + (1 << ENVGENSHIFT)) & ~(NENV - 1);
	if (generation <= 0)	// Don't create a negative env_id.
		generation = 1 << ENVGENSHIFT;
	e->env_id = generation | (e - envs);

	// Set the basic status variables.
    e->proc_lock = 0;
	e->env_parent_id = parent_id;
	proc_set_state(e, PROC_CREATED);
	e->env_runs = 0;
	e->env_refcnt = 1;
	e->env_flags = 0;
	e->env_entry = 0; // cheating.  this really gets set in load_icode
	e->num_vcores = 0;
	memset(&e->vcoremap, -1, sizeof(e->vcoremap));
	//for (int i = 0; i < MAX_NUM_CPUS; i++)
		//e->vcoremap[i] = -1;
	memset(&e->resources, 0, sizeof(e->resources));

	memset(&e->env_ancillary_state, 0, sizeof(e->env_ancillary_state));
	memset(&e->env_tf, 0, sizeof(e->env_tf));
	proc_init_trapframe(&e->env_tf);

	/*
	 * Initialize the contents of the e->env_procinfo structure
	 */
	 e->env_procinfo->id = (e->env_id & 0x3FF);

	/*
	 * Initialize the contents of the e->env_procdata structure
	 */
	// Initialize the generic syscall ring buffer
	SHARED_RING_INIT(&e->env_procdata->syscallring);
	// Initialize the backend of the syscall ring buffer
	BACK_RING_INIT(&e->syscallbackring,
	               &e->env_procdata->syscallring,
	               SYSCALLRINGSIZE);

	// Initialize the generic sysevent ring buffer
	SHARED_RING_INIT(&e->env_procdata->syseventring);
	// Initialize the frontend of the sysevent ring buffer
	FRONT_RING_INIT(&e->syseventfrontring,
	                &e->env_procdata->syseventring,
	                SYSEVENTRINGSIZE);

	*newenv_store = e;
	atomic_inc(&num_envs);

	printk("[%08x] new env %08x\n", current ? current->env_id : 0, e->env_id);
	} // INIT_STRUCT
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
segment_alloc(env_t *e, void *SNT va, size_t len)
{
	void *SNT start, *SNT end;
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
		page_insert(e->env_pgdir, page, start, PTE_USER_RW);
	}
}

//
// Set up the initial program binary, stack, and processor flags
// for a user process.
//
// This function loads all loadable segments from the ELF binary image
// into the environment's user memory, starting at the appropriate
// virtual addresses indicated in the ELF program header.
// At the same time it clears to zero any portions of these segments
// that are marked in the program header as being mapped
// but not actually present in the ELF file - i.e., the program's bss section.
//
// Finally, this function maps one page for the program's initial stack.
static void
load_icode(env_t *SAFE e, uint8_t *COUNT(size) binary, size_t size)
{
	// asw: copy the headers because they might not be aligned.
	elf_t elfhdr;
	proghdr_t phdr;
	memcpy(&elfhdr, binary, sizeof(elfhdr));

	int i, r;

	// is this an elf?
	assert(elfhdr.e_magic == ELF_MAGIC);
	// make sure we have proghdrs to load
	assert(elfhdr.e_phnum);

	// to actually access any pages alloc'd for this environment, we
	// need to have the hardware use this environment's page tables.
	uintreg_t old_cr3 = rcr3();
	/*
	 * Even though we'll decref later and no one should be killing us at this
	 * stage, we're still going to wrap the lcr3s with incref/decref.
	 *
	 * Note we never decref on the old_cr3, since we aren't willing to let it
	 * die.  It's also not clear who the previous process is - sometimes it
	 * isn't even a process (when the kernel loads on its own, and not in
	 * response to a syscall).  Probably need to think more about this (TODO)
	 *
	 * This can get a bit tricky if this code blocks (will need to think about a
	 * decref then), if we try to change states, etc.
	 */
	proc_incref(e);
	lcr3(e->env_cr3);

	// TODO: how do we do a runtime COUNT?
	{TRUSTEDBLOCK // zra: TRUSTEDBLOCK until validation is done.
	for (i = 0; i < elfhdr.e_phnum; i++) {
		memcpy(&phdr, binary + elfhdr.e_phoff + i*sizeof(phdr), sizeof(phdr));
		if (phdr.p_type != ELF_PROG_LOAD)
			continue;
        // TODO: validate elf header fields!
		// seg alloc creates PTE_U|PTE_W pages.  if you ever want to change
		// this, there will be issues with overlapping sections
		segment_alloc(e, (void*SNT)phdr.p_va, phdr.p_memsz);
		memcpy((void*)phdr.p_va, binary + phdr.p_offset, phdr.p_filesz);
		memset((void*)phdr.p_va + phdr.p_filesz, 0, phdr.p_memsz - phdr.p_filesz);
	}}

	proc_set_program_counter(&e->env_tf, elfhdr.e_entry);
	e->env_entry = elfhdr.e_entry;

	// Now map one page for the program's initial stack
	// at virtual address USTACKTOP - PGSIZE.
	segment_alloc(e, (void*SNT)(USTACKTOP - PGSIZE), PGSIZE);

	// reload the original address space
	lcr3(old_cr3);
	proc_decref(e);
}

//
// Allocates a new env and loads the named elf binary into it.
//
env_t* env_create(uint8_t *binary, size_t size)
{
	env_t *e;
	int r;
	envid_t curid;

	curid = (current ? current->env_id : 0);
	if ((r = env_alloc(&e, curid)) < 0)
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
	physaddr_t pa;

	// Note the environment's demise.
	printk("[%08x] free env %08x\n", current ? current->env_id : 0, e->env_id);
	// All parts of the kernel should have decref'd before env_free was called.
	assert(e->env_refcnt == 0);

	// Flush all mapped pages in the user portion of the address space
	env_user_mem_free(e);

	// free the page directory
	pa = e->env_cr3;
	e->env_pgdir = 0;
	e->env_cr3 = 0;
	page_decref(pa2page(pa));

	// return the environment to the free list
	e->state = ENV_FREE;
	spin_lock(&freelist_lock);
	TAILQ_INSERT_HEAD(&proc_freelist, e, proc_link);
	spin_unlock(&freelist_lock);
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


/* This is the top-half of an interrupt handler, where the bottom half is
 * proc_run (which never returns).  Just add it to the delayed work queue,
 * which (incidentally) can only hold one item at this point.
 *
 * Note this is rather old, and meant to run a RUNNABLE_S on a worker core.
 */
#ifdef __IVY__
void run_env_handler(trapframe_t *tf, env_t * data)
#else
void run_env_handler(trapframe_t *tf, void * data)
#endif
{
	assert(data);
	struct work TP(env_t *) job;
	struct workqueue TP(env_t *) *CT(1) workqueue =
	    TC(&per_cpu_info[core_id()].workqueue);
	// this doesn't work, and making it a TP(env_t) is wrong
	// zra: When you want to use other types, let me know, and I can help
    // make something that Ivy is happy with. 
#ifdef __IVY__
	job.func = proc_run;
#else
	job.func = (func_t)proc_run;
#endif
	job.data = data;
	if (enqueue_work(workqueue, &job))
		panic("Failed to enqueue work!");
}
