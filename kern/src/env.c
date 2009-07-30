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
#include <env.h>
#include <pmap.h>
#include <trap.h>
#include <monitor.h>
#include <manager.h>

#include <ros/syscall.h>
#include <ros/error.h>

env_t *envs = NULL;		// All environments
atomic_t num_envs = atomic_init(0);
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
	env_t* curenv = curenvs[core_id()];

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
		return -EBADENV;
	}

	// Check that the calling environment has legitimate permission
	// to manipulate the specified environment.
	// If checkperm is set, the specified environment
	// must be either the current environment
	// or an immediate child of the current environment.
	if (checkperm && e != curenv && e->env_parent_id != curenv->env_id) {
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
//
void
env_init(void)
{
	int i;
	LIST_INIT(&env_free_list);
	assert(envs != NULL);
	for (i = NENV-1; i >= 0; i--) { TRUSTEDBLOCK // asw ivy workaround
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
//	-ENOMEM if page directory or table could not be allocated.
//
static int
env_setup_vm(env_t *e)
WRITES(e->env_pgdir, e->env_cr3, e->env_procinfo, e->env_syscallring,
       e->env_syseventring, e->env_syscallbackring, e->env_syseventfrontring)
{
	int i, r;
	page_t *pgdir = NULL;
	page_t *pginfo = NULL; 
	page_t *pgsyscallring = NULL;
	page_t *pgsyseventring = NULL;

	/* 
	 * Allocate pages for the page directory, shared info, shared data, 
	 * and kernel message pages
	 */
	r = page_alloc(&pgdir);
	if(r < 0) return r;
	r = page_alloc(&pginfo);
	if (r < 0) {
		page_free(pgdir);
		return r;
	}	
	r = page_alloc(&pgsyscallring);
	if (r < 0) {
		page_free(pgdir);
		page_free(pginfo);
		return r;
	}
	r = page_alloc(&pgsyseventring);
	if (r < 0) {
		page_free(pgdir);
		page_free(pginfo);
		page_free(pgsyscallring);
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
	e->env_syscallring = page2kva(pgsyscallring);
	e->env_syseventring = page2kva(pgsyseventring);

	memset(page2kva(pgdir), 0, PGSIZE);
	memset(e->env_procinfo, 0, PGSIZE);
	memset((void*COUNT(PGSIZE)) TC(e->env_syscallring), 0, PGSIZE);
	memset((void*COUNT(PGSIZE)) TC(e->env_syseventring), 0, PGSIZE);

	// Initialize the generic syscall ring buffer
	SHARED_RING_INIT(e->env_syscallring);
	// Initialize the backend of the syscall ring buffer
	BACK_RING_INIT(&e->env_syscallbackring, e->env_syscallring, PGSIZE);
	               
	// Initialize the generic sysevent ring buffer
	SHARED_RING_INIT(e->env_syseventring);
	// Initialize the frontend of the sysevent ring buffer
	FRONT_RING_INIT(&e->env_syseventfrontring, e->env_syseventring, PGSIZE);

	// should be able to do this so long as boot_pgdir never has
	// anything put below UTOP
	memcpy(e->env_pgdir, boot_pgdir, NPDENTRIES*sizeof(pde_t));

	// something like this.  TODO, if you want
	//memcpy(&e->env_pgdir[PDX(UTOP)], &boot_pgdir[PDX(UTOP)], PGSIZE - PDX(UTOP));
	// check with
	// assert(memcmp(e->env_pgdir, boot_pgdir, PGSIZE) == 0);

	// VPT and UVPT map the env's own page table, with
	// different permissions.
	e->env_pgdir[PDX(VPT)]  = PTE(PPN(e->env_cr3), PTE_P | PTE_KERN_RW);
	e->env_pgdir[PDX(UVPT)] = PTE(PPN(e->env_cr3), PTE_P | PTE_USER_RO);

	// Insert the per-process info and ring buffer pages into this process's 
	// pgdir.  I don't want to do these two pages later (like with the stack), 
	// since the kernel wants to keep pointers to it easily.
	// Could place all of this with a function that maps a shared memory page
	// that can work between any two address spaces or something.
	r = page_insert(e->env_pgdir, pginfo, (void*SNT)UINFO, PTE_USER_RO);
	if (r < 0) {
		page_free(pgdir);
		page_free(pginfo);
		page_free(pgsyscallring);
		page_free(pgsyseventring);
		return r;
	}
	r = page_insert(e->env_pgdir, pgsyscallring, (void*SNT)USYSCALL, PTE_USER_RW);
	if (r < 0) {
		// note that we can't currently deallocate the pages created by
		// pgdir_walk (inside insert).  should be able to gather them up when
		// we destroy environments and their page tables.
		page_free(pgdir);
		page_free(pginfo);
		page_free(pgsyscallring);
		page_free(pgsyseventring);
		return r;
	}

	/* Shared page for all processes.  Can't be trusted, but still very useful
	 * at this stage for us.  Consider removing when we have real processes.
	 * (TODO).  Note the page is alloced only the first time through
	 */
	static page_t* shared_page = 0;
	if (!shared_page)
		page_alloc(&shared_page);
	// Up it, so it never goes away.  One per user, plus one from page_alloc
	// This is necessary, since it's in the per-process range of memory that
	// gets freed during page_free.
	shared_page->pp_ref++;

	// Inserted into every process's address space at UGDATA
	page_insert(e->env_pgdir, shared_page, (void*SNT)UGDATA, PTE_USER_RW);

	return 0;
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
	
	//memset((void*)e + sizeof(e->env_link), 0, sizeof(*e) - sizeof(e->env_link));

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
	e->env_status = ENV_RUNNABLE;
	e->env_runs = 0;
	e->env_refcnt = 1;
	e->env_flags = 0;

	memset(&e->env_ancillary_state,0,sizeof(e->env_ancillary_state));
	memset(&e->env_tf,0,sizeof(e->env_tf));
	env_init_trapframe(e);

	// commit the allocation
	LIST_REMOVE(e, env_link);
	*newenv_store = e;
	atomic_inc(&num_envs);

	e->env_tscfreq = system_timing.tsc_freq;
	// TODO: for now, the only info at procinfo is this env's struct
	// note that we need to copy this over every time we make a change to env
	// that we want userspace to see.  also note that we don't even want to
	// show them all of env, only specific things like PID, PPID, etc
	memcpy(e->env_procinfo, e, sizeof(env_t));

	env_t* curenv = curenvs[core_id()];

	printk("[%08x] new env %08x\n", curenv ? curenv->env_id : 0, e->env_id);
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
load_icode(env_t *e, uint8_t *COUNT(size) binary, size_t size)
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

	// asw: copy the headers because they might not be aligned.
	elf_t elfhdr;
	proghdr_t phdr;
	memcpy(&elfhdr,binary,sizeof(elfhdr));

	int i, r;

	// is this an elf?
	assert(elfhdr.e_magic == ELF_MAGIC);
	// make sure we have proghdrs to load
	assert(elfhdr.e_phnum);

	// to actually access any pages alloc'd for this environment, we
	// need to have the hardware use this environment's page tables.
	// we can use e's tables as long as we want, since it has the same
	// mappings for the kernel as does boot_pgdir
	lcr3(e->env_cr3);

	// TODO: how do we do a runtime COUNT?
	{TRUSTEDBLOCK
	for (i = 0; i < elfhdr.e_phnum; i++) {
		memcpy(&phdr,binary+elfhdr.e_phoff+i*sizeof(phdr),sizeof(phdr));
        // zra: TRUSTEDBLOCK until validation is done.
		if (phdr.p_type != ELF_PROG_LOAD)
			continue;
        // TODO: validate elf header fields!
		// seg alloc creates PTE_U|PTE_W pages.  if you ever want to change
		// this, there will be issues with overlapping sections
		segment_alloc(e, (void*SNT)phdr.p_va, phdr.p_memsz);
		memcpy((void*)phdr.p_va, binary + phdr.p_offset, phdr.p_filesz);
		memset((void*)phdr.p_va + phdr.p_filesz, 0, phdr.p_memsz - phdr.p_filesz);
	}}

	env_set_program_counter(e,elfhdr.e_entry);

	// Now map one page for the program's initial stack
	// at virtual address USTACKTOP - PGSIZE.

	segment_alloc(e, (void*SNT)(USTACKTOP - PGSIZE), PGSIZE);
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
	physaddr_t pa;

	// Note the environment's demise.
	env_t* curenv = curenvs[core_id()];
	cprintf("[%08x] free env %08x\n", curenv ? curenv->env_id : 0, e->env_id);

	// Flush all mapped pages in the user portion of the address space
	env_user_mem_free(e);

	// Moved to page_decref
	// need a known good pgdir before releasing the old one
	//lcr3(PADDR(boot_pgdir));

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
		retval = -EBADENV;
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
	// need a known good pgdir before releasing the old one
	// sometimes env_free is called on a different core than decref
	lcr3(PADDR(boot_pgdir));

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
	atomic_dec(&num_envs);

	// for old envs that die on user cores.  since env run never returns, cores
	// never get back to their old hlt/relaxed/spin state, so we need to force
	// them back to an idle function.
	uint32_t id = core_id();
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
		// TODO: race here, if another core is just about to start this env.
		// Fix it by setting the status in something like env_dispatch when
		// we have multi-contexted processes
		if (e && e->env_status == ENV_RUNNABLE) {
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
	if (e != curenvs[core_id()]) {
		curenvs[core_id()] = e;
		e->env_runs++;
		lcr3(e->env_cr3);
	}

	env_pop_ancillary_state(e);

	env_pop_tf(&e->env_tf);
}

/* This is the top-half of an interrupt handler, where the bottom half is
 * env_run (which never returns).  Just add it to the delayed work queue,
 * which isn't really a queue yet.
 */
void run_env_handler(trapframe_t *tf, void* data)
{
	assert(data);
	per_cpu_info_t *cpuinfo = &per_cpu_info[core_id()];
	spin_lock_irqsave(&cpuinfo->lock);
	{ TRUSTEDBLOCK // TODO: how do we make this func_t cast work?
	cpuinfo->delayed_work.func = (func_t)env_run;
	cpuinfo->delayed_work.data = data;
	}
	spin_unlock_irqsave(&cpuinfo->lock);
}
