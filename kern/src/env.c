/* See COPYRIGHT for copyright information. */
#ifdef __DEPUTY__
//#pragma nodeputy
#pragma noasync
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

#include <ros/syscall.h>
#include <ros/error.h>

env_t *envs = NULL;		// All environments
atomic_t num_envs;
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
// and insert them into the env_free_list.
// Insert in reverse order, so that the first call to env_alloc()
// returns envs[0].
// TODO: get rid of this whole array bullshit
//
void
env_init(void)
{
	int i;

	atomic_init(&num_envs, 0);
	LIST_INIT(&env_free_list);
	assert(envs != NULL);
	for (i = NENV-1; i >= 0; i--) { TRUSTEDBLOCK // asw ivy workaround
		// these should already be set from when i memset'd the array to 0
		envs[i].state = ENV_FREE;
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
	static page_t* shared_page = 0;

	/* 
	 * First, allocate a page for the pgdir of this process and up 
	 * its reference count since this will never be done elsewhere
	 */
	r = page_alloc(&pgdir);
	if(r < 0) return r;
	pgdir->pp_ref++;

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
		if(page_insert(e->env_pgdir, pginfo[i], (void*SNT)(UINFO + i*PGSIZE), PTE_USER_RO) < 0)
			goto env_setup_vm_error;
	}
	
	/*
	 * Now allocate and insert all pages required for the shared 
	 * procdata structure into the page table
	 */
	for(int i=0; i<PROCDATA_NUM_PAGES; i++) {
		if(page_alloc(&pgdata[i]) < 0)
			goto env_setup_vm_error;
		if(page_insert(e->env_pgdir, pgdata[i], (void*SNT)(UDATA + i*PGSIZE), PTE_USER_RW) < 0)
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
	shared_page->pp_ref++;
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

	if (!(e = LIST_FIRST(&env_free_list)))
		return -ENOFREEENV;
	
    { INITSTRUCT(*e)

	// Allocate and set up the page directory for this environment.
	if ((r = env_setup_vm(e)) < 0)
		return r;

	// Generate an env_id for this environment.
	generation = (e->env_id + (1 << ENVGENSHIFT)) & ~(NENV - 1);
	if (generation <= 0)	// Don't create a negative env_id.
		generation = 1 << ENVGENSHIFT;
	e->env_id = generation | (e - envs);

	// Set the basic status variables.
    e->lock = 0;
	e->env_parent_id = parent_id;
	proc_set_state(e, PROC_CREATED);
	e->env_runs = 0;
	e->env_refcnt = 1;
	e->env_flags = 0;


	memset(&e->env_ancillary_state, 0, sizeof(e->env_ancillary_state));
	memset(&e->env_tf, 0, sizeof(e->env_tf));
	env_init_trapframe(e);

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

	// commit the allocation
	LIST_REMOVE(e, env_link);
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
	env_incref(e);
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

	env_set_program_counter(e, elfhdr.e_entry);

	// Now map one page for the program's initial stack
	// at virtual address USTACKTOP - PGSIZE.
	segment_alloc(e, (void*SNT)(USTACKTOP - PGSIZE), PGSIZE);

	// reload the original address space
	lcr3(old_cr3);
	env_decref(e);
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
	LIST_INSERT_HEAD(&env_free_list, e, env_link);
}

/*
 * The process refcnt is the number of places the process 'exists' in the
 * system.  Creation counts as 1.  Having your page tables loaded somewhere
 * (lcr3) counts as another 1.  A non-RUNNING_* process should have refcnt at
 * least 1.  If the kernel is on another core and in a processes address space
 * (like processing its backring), that counts as another 1.
 *
 * Note that the actual loading and unloading of cr3 is up to the caller, since
 * that's not the only use for this (and decoupling is more flexible).
 *
 * The refcnt should always be greater than 0 for processes that aren't dying.
 * When refcnt is 0, the process is dying and should not allow any more increfs.
 * A process can be dying with a refcnt greater than 0, since it could be
 * waiting for other cores to "get the message" to die, or a kernel core can be
 * finishing work in the processes's address space.
 *
 * Implementation aside, the important thing is that we atomically increment
 * only if it wasn't already 0.  If it was 0, then we shouldn't be attaching to
 * the process, so we return an error, which should be handled however is
 * appropriate.  We currently use spinlocks, but some sort of clever atomics
 * would work too.
 *
 * Also, no one should ever update the refcnt outside of these functions.
 * Eventually, we'll have Ivy support for this. (TODO)
 */
error_t env_incref(env_t* e)
{
	error_t retval = 0;
	spin_lock_irqsave(&e->lock);
	if (e->env_refcnt)
		e->env_refcnt++;
	else
		retval = -EBADENV;
	spin_unlock_irqsave(&e->lock);
	return retval;
}

/*
 * When the kernel is done with a process, it decrements its reference count.
 * When the count hits 0, no one is using it and it should be freed.
 * "Last one out" actually finalizes the death of the process.  This is tightly
 * coupled with the previous function (incref)
 * Be sure to load a different cr3 before calling this!
 */
void env_decref(env_t* e)
{
	spin_lock_irqsave(&e->lock);
	e->env_refcnt--;
	spin_unlock_irqsave(&e->lock);
	// if we hit 0, no one else will increment and we can check outside the lock
	if (e->env_refcnt == 0)
		env_free(e);
}


/*
 * Destroys the given process.  Can be called by a different process (checked
 * via current), though that's unable to handle an async call (TODO current does
 * not work asyncly, though it could be made to in the async processing
 * function. 
 */
void
env_destroy(env_t *e)
{
	// TODO: XME race condition with env statuses, esp when running / destroying
	proc_set_state(e, PROC_DYING);

	/*
	 * If we are currently running this address space on our core, we need a
	 * known good pgdir before releasing the old one.  This is currently the
	 * major practical implication of the kernel caring about a processes
	 * existence (the inc and decref).  This decref corresponds to the incref in
	 * proc_startcore (though it's not the only one).
	 */
	if (current == e) {
		lcr3(boot_cr3);
		env_decref(e); // this decref is for the cr3
	}
	env_decref(e); // this decref is for the process in general
	atomic_dec(&num_envs);

	/*
	 * Could consider removing this from destroy and having the caller specify
	 * these actions
	 */
	// for old envs that die on user cores.  since env run never returns, cores
	// never get back to their old hlt/relaxed/spin state, so we need to force
	// them back to an idle function.
	uint32_t id = core_id();
	// There is no longer a current process for this core. (TODO: Think about this.)
	current = NULL;
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
}

/* ugly, but for now just linearly search through all possible
 * environments for a runnable one.
 * the current *policy* is to round-robin the search
 */
void schedule(void)
{
	env_t *e;
	static int last_picked = 0;
	
	for (int i = 0, j = last_picked + 1; i < NENV; i++, j = (j + 1) % NENV) {
		e = &envs[ENVX(j)];
		// TODO: XME race here, if another core is just about to start this env.
		// Fix it by setting the status in something like env_dispatch when
		// we have multi-contexted processes
		if (e && e->state == PROC_RUNNABLE_S) {
			last_picked = j;
			env_run(e);
		}
	}

	cprintf("Destroyed the only environment - nothing more to do!\n");
	while (1)
		monitor(NULL);
}

//
// Context switch from curenv to env e.
// Note: if this is the first call to env_run, curenv is NULL.
//  (This function does not return.)
//
void
env_run(env_t *e)
{
	// TODO: XME race here with env destroy on the status and refcnt
	// Could up the refcnt and down it when a process is not running
	
	proc_set_state(e, PROC_RUNNING_S);
	proc_startcore(e, &e->env_tf);
}

/* This is the top-half of an interrupt handler, where the bottom half is
 * env_run (which never returns).  Just add it to the delayed work queue,
 * which (incidentally) can only hold one item at this point.
 */
void run_env_handler(trapframe_t *tf, void *data)
{
	assert(data);
	struct work job;
	struct workqueue *workqueue = &per_cpu_info[core_id()].workqueue;
	{ TRUSTEDBLOCK // TODO: how do we make this func_t cast work?
	job.func = (func_t)env_run;
	job.data = data;
	}
	if (enqueue_work(workqueue, &job))
		panic("Failed to enqueue work!");
}
