/* Copyright (c) 2009, 2010 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details. */

#ifdef __SHARC__
#pragma nosharc
#endif

#include <ros/bcq.h>
#include <event.h>
#include <arch/arch.h>
#include <bitmask.h>
#include <process.h>
#include <atomic.h>
#include <smp.h>
#include <pmap.h>
#include <trap.h>
#include <schedule.h>
#include <manager.h>
#include <stdio.h>
#include <assert.h>
#include <timing.h>
#include <hashtable.h>
#include <slab.h>
#include <sys/queue.h>
#include <frontend.h>
#include <monitor.h>
#include <resource.h>
#include <elf.h>
#include <arsc_server.h>
#include <devfs.h>

/* Process Lists */
struct proc_list proc_runnablelist = TAILQ_HEAD_INITIALIZER(proc_runnablelist);
spinlock_t runnablelist_lock = SPINLOCK_INITIALIZER;
struct kmem_cache *proc_cache;

/* Tracks which cores are idle, similar to the vcoremap.  Each value is the
 * physical coreid of an unallocated core. */
spinlock_t idle_lock = SPINLOCK_INITIALIZER;
uint32_t LCKD(&idle_lock) (RO idlecoremap)[MAX_NUM_CPUS];
uint32_t LCKD(&idle_lock) num_idlecores = 0;
uint32_t num_mgmtcores = 1;

/* Helper function to return a core to the idlemap.  It causes some more lock
 * acquisitions (like in a for loop), but it's a little easier.  Plus, one day
 * we might be able to do this without locks (for the putting). */
void put_idle_core(uint32_t coreid)
{
	spin_lock(&idle_lock);
	idlecoremap[num_idlecores++] = coreid;
	spin_unlock(&idle_lock);
}

/* Other helpers, implemented later. */
static void __proc_startcore(struct proc *p, trapframe_t *tf);
static uint32_t get_free_vcoreid(struct proc *SAFE p, uint32_t prev);
static uint32_t get_busy_vcoreid(struct proc *SAFE p, uint32_t prev);
static bool is_mapped_vcore(struct proc *p, uint32_t pcoreid);
static uint32_t get_vcoreid(struct proc *p, uint32_t pcoreid);
static uint32_t get_pcoreid(struct proc *p, uint32_t vcoreid);
static void __proc_free(struct kref *kref);

/* PID management. */
#define PID_MAX 32767 // goes from 0 to 32767, with 0 reserved
static DECL_BITMASK(pid_bmask, PID_MAX + 1);
spinlock_t pid_bmask_lock = SPINLOCK_INITIALIZER;
struct hashtable *pid_hash;
spinlock_t pid_hash_lock; // initialized in proc_init

/* Finds the next free entry (zero) entry in the pid_bitmask.  Set means busy.
 * PID 0 is reserved (in proc_init).  A return value of 0 is a failure (and
 * you'll also see a warning, for now).  Consider doing this with atomics. */
static pid_t get_free_pid(void)
{
	static pid_t next_free_pid = 1;
	pid_t my_pid = 0;

	spin_lock(&pid_bmask_lock);
	// atomically (can lock for now, then change to atomic_and_return
	FOR_CIRC_BUFFER(next_free_pid, PID_MAX + 1, i) {
		// always points to the next to test
		next_free_pid = (next_free_pid + 1) % (PID_MAX + 1);
		if (!GET_BITMASK_BIT(pid_bmask, i)) {
			SET_BITMASK_BIT(pid_bmask, i);
			my_pid = i;
			break;
		}
	}
	spin_unlock(&pid_bmask_lock);
	if (!my_pid)
		warn("Shazbot!  Unable to find a PID!  You need to deal with this!\n");
	return my_pid;
}

/* Return a pid to the pid bitmask */
static void put_free_pid(pid_t pid)
{
	spin_lock(&pid_bmask_lock);
	CLR_BITMASK_BIT(pid_bmask, pid);
	spin_unlock(&pid_bmask_lock);
}

/* While this could be done with just an assignment, this gives us the
 * opportunity to check for bad transitions.  Might compile these out later, so
 * we shouldn't rely on them for sanity checking from userspace.  */
int __proc_set_state(struct proc *p, uint32_t state)
{
	uint32_t curstate = p->state;
	/* Valid transitions:
	 * C   -> RBS
	 * C   -> D
	 * RBS -> RGS
	 * RGS -> RBS
	 * RGS -> W
	 * W   -> RBS
	 * RGS -> RBM
	 * RBM -> RGM
	 * RGM -> RBM
	 * RGM -> RBS
	 * RGS -> D
	 * RGM -> D
	 *
	 * These ought to be implemented later (allowed, not thought through yet).
	 * RBS -> D
	 * RBM -> D
	 */
	#if 1 // some sort of correctness flag
	switch (curstate) {
		case PROC_CREATED:
			if (!(state & (PROC_RUNNABLE_S | PROC_DYING)))
				panic("Invalid State Transition! PROC_CREATED to %02x", state);
			break;
		case PROC_RUNNABLE_S:
			if (!(state & (PROC_RUNNING_S | PROC_DYING)))
				panic("Invalid State Transition! PROC_RUNNABLE_S to %02x", state);
			break;
		case PROC_RUNNING_S:
			if (!(state & (PROC_RUNNABLE_S | PROC_RUNNABLE_M | PROC_WAITING |
			               PROC_DYING)))
				panic("Invalid State Transition! PROC_RUNNING_S to %02x", state);
			break;
		case PROC_WAITING:
			if (state != PROC_RUNNABLE_S)
				panic("Invalid State Transition! PROC_WAITING to %02x", state);
			break;
		case PROC_DYING:
			if (state != PROC_CREATED) // when it is reused (TODO)
				panic("Invalid State Transition! PROC_DYING to %02x", state);
			break;
		case PROC_RUNNABLE_M:
			if (!(state & (PROC_RUNNING_M | PROC_DYING)))
				panic("Invalid State Transition! PROC_RUNNABLE_M to %02x", state);
			break;
		case PROC_RUNNING_M:
			if (!(state & (PROC_RUNNABLE_S | PROC_RUNNABLE_M | PROC_DYING)))
				panic("Invalid State Transition! PROC_RUNNING_M to %02x", state);
			break;
	}
	#endif
	p->state = state;
	return 0;
}

/* Returns a pointer to the proc with the given pid, or 0 if there is none.
 * This uses get_not_zero, since it is possible the refcnt is 0, which means the
 * process is dying and we should not have the ref (and thus return 0).  We need
 * to lock to protect us from getting p, (someone else removes and frees p),
 * then get_not_zero() on p.
 * Don't push the locking into the hashtable without dealing with this. */
struct proc *pid2proc(pid_t pid)
{
	spin_lock(&pid_hash_lock);
	struct proc *p = hashtable_search(pid_hash, (void*)pid);
	if (p)
		if (!kref_get_not_zero(&p->p_kref, 1))
			p = 0;
	spin_unlock(&pid_hash_lock);
	return p;
}

/* Performs any initialization related to processes, such as create the proc
 * cache, prep the scheduler, etc.  When this returns, we should be ready to use
 * any process related function. */
void proc_init(void)
{
	proc_cache = kmem_cache_create("proc", sizeof(struct proc),
	             MAX(HW_CACHE_ALIGN, __alignof__(struct proc)), 0, 0, 0);
	/* Init PID mask and hash.  pid 0 is reserved. */
	SET_BITMASK_BIT(pid_bmask, 0);
	spinlock_init(&pid_hash_lock);
	spin_lock(&pid_hash_lock);
	pid_hash = create_hashtable(100, __generic_hash, __generic_eq);
	spin_unlock(&pid_hash_lock);
	schedule_init();
	/* Init idle cores. Core 0 is the management core. */
	spin_lock(&idle_lock);
#ifdef __CONFIG_DISABLE_SMT__
	/* assumes core0 is the only management core (NIC and monitor functionality
	 * are run there too.  it just adds the odd cores to the idlecoremap */
	assert(!(num_cpus % 2));
	// TODO: consider checking x86 for machines that actually hyperthread
	num_idlecores = num_cpus >> 1;
#ifdef __CONFIG_ARSC_SERVER__
	// Dedicate one core (core 2) to sysserver, might be able to share wit NIC
	num_mgmtcores++;
	assert(num_cpus >= num_mgmtcores);
	send_kernel_message(2, (amr_t)arsc_server, 0,0,0, KMSG_ROUTINE);
#endif
	for (int i = 0; i < num_idlecores; i++)
		idlecoremap[i] = (i * 2) + 1;
#else
	// __CONFIG_DISABLE_SMT__
	#ifdef __CONFIG_NETWORKING__
	num_mgmtcores++; // Next core is dedicated to the NIC
	assert(num_cpus >= num_mgmtcores);
	#endif
	#ifdef __CONFIG_APPSERVER__
	#ifdef __CONFIG_DEDICATED_MONITOR__
	num_mgmtcores++; // Next core dedicated to running the kernel monitor
	assert(num_cpus >= num_mgmtcores);
	// Need to subtract 1 from the num_mgmtcores # to get the cores index
	send_kernel_message(num_mgmtcores-1, (amr_t)monitor, 0,0,0, KMSG_ROUTINE);
	#endif
	#endif
#ifdef __CONFIG_ARSC_SERVER__
	// Dedicate one core (core 2) to sysserver, might be able to share wit NIC
	num_mgmtcores++;
	assert(num_cpus >= num_mgmtcores);
	send_kernel_message(num_mgmtcores-1, (amr_t)arsc_server, 0,0,0, KMSG_ROUTINE);
#endif
	num_idlecores = num_cpus - num_mgmtcores;
	for (int i = 0; i < num_idlecores; i++)
		idlecoremap[i] = i + num_mgmtcores;
#endif /* __CONFIG_DISABLE_SMT__ */

	spin_unlock(&idle_lock);
	atomic_init(&num_envs, 0);
}

void
proc_init_procinfo(struct proc* p)
{
	memset(&p->procinfo->vcoremap, 0, sizeof(p->procinfo->vcoremap));
	memset(&p->procinfo->pcoremap, 0, sizeof(p->procinfo->pcoremap));
	p->procinfo->num_vcores = 0;
	p->procinfo->coremap_seqctr = SEQCTR_INITIALIZER;
	// TODO: change these too
	p->procinfo->pid = p->pid;
	p->procinfo->ppid = p->ppid;
	p->procinfo->tsc_freq = system_timing.tsc_freq;
	// TODO: maybe do something smarter here
#ifdef __CONFIG_DISABLE_SMT__
	p->procinfo->max_vcores = num_cpus >> 1;
#else
	p->procinfo->max_vcores = MAX(1,num_cpus-num_mgmtcores);
#endif /* __CONFIG_DISABLE_SMT__ */
}

/* Allocates and initializes a process, with the given parent.  Currently
 * writes the *p into **pp, and returns 0 on success, < 0 for an error.
 * Errors include:
 *  - ENOFREEPID if it can't get a PID
 *  - ENOMEM on memory exhaustion */
error_t proc_alloc(struct proc **pp, struct proc *parent)
{
	error_t r;
	struct proc *p;

	if (!(p = kmem_cache_alloc(proc_cache, 0)))
		return -ENOMEM;

	{ INITSTRUCT(*p)

	/* one reference for the proc existing, and one for the ref we pass back. */
	kref_init(&p->p_kref, __proc_free, 2);
	// Setup the default map of where to get cache colors from
	p->cache_colors_map = global_cache_colors_map;
	p->next_cache_color = 0;
	/* Initialize the address space */
	if ((r = env_setup_vm(p)) < 0) {
		kmem_cache_free(proc_cache, p);
		return r;
	}
	if (!(p->pid = get_free_pid())) {
		kmem_cache_free(proc_cache, p);
		return -ENOFREEPID;
	}
	/* Set the basic status variables. */
	spinlock_init(&p->proc_lock);
	p->exitcode = 1337;	/* so we can see processes killed by the kernel */
	p->ppid = parent ? parent->pid : 0;
	p->state = PROC_CREATED; /* shouldn't go through state machine for init */
	p->env_flags = 0;
	p->env_entry = 0; // cheating.  this really gets set later
	p->procinfo->heap_bottom = (void*)UTEXT;
	p->heap_top = (void*)UTEXT;
	memset(&p->resources, 0, sizeof(p->resources));
	memset(&p->env_ancillary_state, 0, sizeof(p->env_ancillary_state));
	memset(&p->env_tf, 0, sizeof(p->env_tf));
	TAILQ_INIT(&p->vm_regions); /* could init this in the slab */

	/* Initialize the contents of the e->procinfo structure */
	proc_init_procinfo(p);
	/* Initialize the contents of the e->procdata structure */

	/* Initialize the generic sysevent ring buffer */
	SHARED_RING_INIT(&p->procdata->syseventring);
	/* Initialize the frontend of the sysevent ring buffer */
	FRONT_RING_INIT(&p->syseventfrontring,
	                &p->procdata->syseventring,
	                SYSEVENTRINGSIZE);

	/* Init FS structures TODO: cleanup (might pull this out) */
	kref_get(&default_ns.kref, 1);
	p->ns = &default_ns;
	spinlock_init(&p->fs_env.lock);
	p->fs_env.umask = parent ? parent->fs_env.umask : S_IWGRP | S_IWOTH;
	p->fs_env.root = p->ns->root->mnt_root;
	kref_get(&p->fs_env.root->d_kref, 1);
	p->fs_env.pwd = parent ? parent->fs_env.pwd : p->fs_env.root;
	kref_get(&p->fs_env.pwd->d_kref, 1);
	memset(&p->open_files, 0, sizeof(p->open_files));	/* slightly ghetto */
	spinlock_init(&p->open_files.lock);
	p->open_files.max_files = NR_OPEN_FILES_DEFAULT;
	p->open_files.max_fdset = NR_FILE_DESC_DEFAULT;
	p->open_files.fd = p->open_files.fd_array;
	p->open_files.open_fds = (struct fd_set*)&p->open_files.open_fds_init;

	atomic_inc(&num_envs);
	frontend_proc_init(p);
	printd("[%08x] new process %08x\n", current ? current->pid : 0, p->pid);
	} // INIT_STRUCT
	*pp = p;
	return 0;
}

/* We have a bunch of different ways to make processes.  Call this once the
 * process is ready to be used by the rest of the system.  For now, this just
 * means when it is ready to be named via the pidhash.  In the future, we might
 * push setting the state to CREATED into here. */
void __proc_ready(struct proc *p)
{
	spin_lock(&pid_hash_lock);
	hashtable_insert(pid_hash, (void*)p->pid, p);
	spin_unlock(&pid_hash_lock);
}

/* Creates a process from the specified file, argvs, and envps.  Tempted to get
 * rid of proc_alloc's style, but it is so quaint... */
struct proc *proc_create(struct file *prog, char **argv, char **envp)
{
	struct proc *p;
	error_t r;
	if ((r = proc_alloc(&p, current)) < 0)
		panic("proc_create: %e", r);	/* one of 3 quaint usages of %e */
	procinfo_pack_args(p->procinfo, argv, envp);
	assert(load_elf(p, prog) == 0);
	/* Connect to stdin, stdout, stderr */
	assert(insert_file(&p->open_files, dev_stdin,  0) == 0);
	assert(insert_file(&p->open_files, dev_stdout, 0) == 1);
	assert(insert_file(&p->open_files, dev_stderr, 0) == 2);
	__proc_ready(p);
	return p;
}

/* This is called by kref_put(), once the last reference to the process is
 * gone.  Don't call this otherwise (it will panic).  It will clean up the
 * address space and deallocate any other used memory. */
static void __proc_free(struct kref *kref)
{
	struct proc *p = container_of(kref, struct proc, p_kref);
	physaddr_t pa;

	printd("[PID %d] freeing proc: %d\n", current ? current->pid : 0, p->pid);
	// All parts of the kernel should have decref'd before __proc_free is called
	assert(kref_refcnt(&p->p_kref) == 0);

	kref_put(&p->fs_env.root->d_kref);
	kref_put(&p->fs_env.pwd->d_kref);
	destroy_vmrs(p);
	frontend_proc_free(p);	/* TODO: please remove me one day */
	/* Free any colors allocated to this process */
	if (p->cache_colors_map != global_cache_colors_map) {
		for(int i = 0; i < llc_cache->num_colors; i++)
			cache_color_free(llc_cache, p->cache_colors_map);
		cache_colors_map_free(p->cache_colors_map);
	}
	/* Remove us from the pid_hash and give our PID back (in that order). */
	spin_lock(&pid_hash_lock);
	if (!hashtable_remove(pid_hash, (void*)p->pid))
		panic("Proc not in the pid table in %s", __FUNCTION__);
	spin_unlock(&pid_hash_lock);
	put_free_pid(p->pid);
	/* Flush all mapped pages in the user portion of the address space */
	env_user_mem_free(p, 0, UVPT);
	/* These need to be free again, since they were allocated with a refcnt. */
	free_cont_pages(p->procinfo, LOG2_UP(PROCINFO_NUM_PAGES));
	free_cont_pages(p->procdata, LOG2_UP(PROCDATA_NUM_PAGES));

	env_pagetable_free(p);
	p->env_pgdir = 0;
	p->env_cr3 = 0;

	atomic_dec(&num_envs);

	/* Dealloc the struct proc */
	kmem_cache_free(proc_cache, p);
}

/* Whether or not actor can control target.  Note we currently don't need
 * locking for this. TODO: think about that, esp wrt proc's dying. */
bool proc_controls(struct proc *actor, struct proc *target)
{
	return ((actor == target) || (target->ppid == actor->pid));
}

/* Helper to incref by val.  Using the helper to help debug/interpose on proc
 * ref counting.  Note that pid2proc doesn't use this interface. */
void proc_incref(struct proc *p, unsigned int val)
{
	kref_get(&p->p_kref, val);
}

/* Helper to decref for debugging.  Don't directly kref_put() for now. */
void proc_decref(struct proc *p)
{
	kref_put(&p->p_kref);
}

/* Helper, makes p the 'current' process, dropping the old current/cr3.  Don't
 * incref - this assumes the passed in reference already counted 'current'. */
static void __set_proc_current(struct proc *p)
{
	/* We use the pcpui to access 'current' to cut down on the core_id() calls,
	 * though who know how expensive/painful they are. */
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	/* If the process wasn't here, then we need to load its address space. */
	if (p != pcpui->cur_proc) {
		/* Do not incref here.  We were given the reference to current,
		 * pre-upped. */
		lcr3(p->env_cr3);
		/* This is "leaving the process context" of the previous proc.  The
		 * previous lcr3 unloaded the previous proc's context.  This should
		 * rarely happen, since we usually proactively leave process context,
		 * but this is the fallback. */
		if (pcpui->cur_proc)
			proc_decref(pcpui->cur_proc);
		pcpui->cur_proc = p;
	}
}

/* Dispatches a process to run, either on the current core in the case of a
 * RUNNABLE_S, or on its partition in the case of a RUNNABLE_M.  This should
 * never be called to "restart" a core.  This expects that the "instructions"
 * for which core(s) to run this on will be in the vcoremap, which needs to be
 * set externally.
 *
 * When a process goes from RUNNABLE_M to RUNNING_M, its vcoremap will be
 * "packed" (no holes in the vcore->pcore mapping), vcore0 will continue to run
 * it's old core0 context, and the other cores will come in at the entry point.
 * Including in the case of preemption.
 *
 * This won't return if the current core is going to be one of the processes
 * cores (either for _S mode or for _M if it's in the vcoremap).  proc_run will
 * eat your reference if it does not return. */
void proc_run(struct proc *p)
{
	bool self_ipi_pending = FALSE;
	spin_lock(&p->proc_lock);

	switch (p->state) {
		case (PROC_DYING):
			spin_unlock(&p->proc_lock);
			printk("Process %d not starting due to async death\n", p->pid);
			// if we're a worker core, smp_idle, o/w return
			if (!management_core())
				smp_idle(); // this never returns
			return;
		case (PROC_RUNNABLE_S):
			assert(current != p);
			__proc_set_state(p, PROC_RUNNING_S);
			/* We will want to know where this process is running, even if it is
			 * only in RUNNING_S.  can use the vcoremap, which makes death easy.
			 * Also, this is the signal used in trap.c to know to save the tf in
			 * env_tf. */
			__seq_start_write(&p->procinfo->coremap_seqctr);
			p->procinfo->num_vcores = 0;
			__map_vcore(p, 0, core_id()); // sort of.  this needs work.
			__seq_end_write(&p->procinfo->coremap_seqctr);
			/* __set_proc_current assumes the reference we give it is for
			 * current.  Decref if current is already properly set, otherwise
			 * ensure current is set. */
			if (p == current)
				proc_decref(p);
			else
				__set_proc_current(p);
			/* We restartcore, instead of startcore, since startcore is a bit
			 * lower level and we want a chance to process kmsgs before starting
			 * the process. */
			spin_unlock(&p->proc_lock);
			current_tf = &p->env_tf;
			proc_restartcore();
			break;
		case (PROC_RUNNABLE_M):
			/* vcoremap[i] holds the coreid of the physical core allocated to
			 * this process.  It is set outside proc_run.  For the kernel
			 * message, a0 = struct proc*, a1 = struct trapframe*.   */
			if (p->procinfo->num_vcores) {
				__proc_set_state(p, PROC_RUNNING_M);
				/* Up the refcnt, since num_vcores are going to start using this
				 * process and have it loaded in their 'current'. */
				proc_incref(p, p->procinfo->num_vcores);
				/* If the core we are running on is in the vcoremap, we will get
				 * an IPI (once we reenable interrupts) and never return. */
				if (is_mapped_vcore(p, core_id()))
					self_ipi_pending = TRUE;
				for (int i = 0; i < p->procinfo->num_vcores; i++)
					send_kernel_message(get_pcoreid(p, i), __startcore, p, 0,
					                    0, KMSG_ROUTINE);
			} else {
				warn("Tried to proc_run() an _M with no vcores!");
			}
			/* Unlock and decref/wait for the IPI if one is pending.  This will
			 * eat the reference if we aren't returning.
			 *
			 * There a subtle race avoidance here.  __proc_startcore can handle
			 * a death message, but we can't have the startcore come after the
			 * death message.  Otherwise, it would look like a new process.  So
			 * we hold the lock til after we send our message, which prevents a
			 * possible death message.
			 * - Note there is no guarantee this core's interrupts were on, so
			 *   it may not get the message for a while... */
			spin_unlock(&p->proc_lock);
			__proc_kmsg_pending(p, self_ipi_pending);
			break;
		default:
			spin_unlock(&p->proc_lock);
			panic("Invalid process state %p in proc_run()!!", p->state);
	}
}

/* Actually runs the given context (trapframe) of process p on the core this
 * code executes on.  This is called directly by __startcore, which needs to
 * bypass the routine_kmsg check.  Interrupts should be off when you call this.
 *
 * A note on refcnting: this function will not return, and your proc reference
 * will end up stored in current.  This will make no changes to p's refcnt, so
 * do your accounting such that there is only the +1 for current.  This means if
 * it is already in current (like in the trap return path), don't up it.  If
 * it's already in current and you have another reference (like pid2proc or from
 * an IPI), then down it (which is what happens in __startcore()).  If it's not
 * in current and you have one reference, like proc_run(non_current_p), then
 * also do nothing.  The refcnt for your *p will count for the reference stored
 * in current. */
static void __proc_startcore(struct proc *p, trapframe_t *tf)
{
	assert(!irq_is_enabled());
	__set_proc_current(p);
	/* need to load our silly state, preferably somewhere other than here so we
	 * can avoid the case where the context was just running here.  it's not
	 * sufficient to do it in the "new process" if-block above (could be things
	 * like page faults that cause us to keep the same process, but want a
	 * different context.
	 * for now, we load this silly state here. (TODO) (HSS)
	 * We also need this to be per trapframe, and not per process...
	 * For now / OSDI, only load it when in _S mode.  _M mode was handled in
	 * __startcore.  */
	if (p->state == PROC_RUNNING_S)
		env_pop_ancillary_state(p);
	/* Clear the current_tf, since it is no longer used */
	current_tf = 0;
	env_pop_tf(tf);
}

/* Restarts/runs the current_tf, which must be for the current process, on the
 * core this code executes on.  Calls an internal function to do the work.
 *
 * In case there are pending routine messages, like __death, __preempt, or
 * __notify, we need to run them.  Alternatively, if there are any, we could
 * self_ipi, and run the messages immediately after popping back to userspace,
 * but that would have crappy overhead.
 *
 * Refcnting: this will not return, and it assumes that you've accounted for
 * your reference as if it was the ref for "current" (which is what happens when
 * returning from local traps and such. */
void proc_restartcore(void)
{
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	/* If there is no cur_tf, it is because the old one was already restarted
	 * (and we weren't interrupting another one to finish).  In which case, we
	 * should just smp_idle() */
	if (!pcpui->cur_tf) {
		/* It is possible for us to have current loaded if a kthread restarted
		 * after the process yielded the core. */
		abandon_core();
		smp_idle();
	}
	/* TODO: this is where we can decide to smp_idle() if there is no cur_tf */
	/* Need ints disabled when we return from processing (race) */
	disable_irq();
	/* Need to be current (set by the caller), in case a kmsg is there that
	 * tries to clobber us. */
	process_routine_kmsg(pcpui->cur_tf);
	__proc_startcore(pcpui->cur_proc, pcpui->cur_tf);
}

/*
 * Destroys the given process.  This may be called from another process, a light
 * kernel thread (no real process context), asynchronously/cross-core, or from
 * the process on its own core.
 *
 * Here's the way process death works:
 * 0. grab the lock (protects state transition and core map)
 * 1. set state to dying.  that keeps the kernel from doing anything for the
 * process (like proc_running it).
 * 2. figure out where the process is running (cross-core/async or RUNNING_M)
 * 3. IPI to clean up those cores (decref, etc).
 * 4. Unlock
 * 5. Clean up your core, if applicable
 * (Last core/kernel thread to decref cleans up and deallocates resources.)
 *
 * Note that some cores can be processing async calls, but will eventually
 * decref.  Should think about this more, like some sort of callback/revocation.
 *
 * This will eat your reference if it won't return.  Note that this function
 * needs to change anyways when we make __death more like __preempt.  (TODO) */
void proc_destroy(struct proc *p)
{
	bool self_ipi_pending = FALSE;
	
	spin_lock(&p->proc_lock);
	/* TODO: (DEATH) look at this again when we sort the __death IPI */
	if (current == p)
		self_ipi_pending = TRUE;

	switch (p->state) {
		case PROC_DYING: // someone else killed this already.
			spin_unlock(&p->proc_lock);
			__proc_kmsg_pending(p, self_ipi_pending);
			return;
		case PROC_RUNNABLE_M:
			/* Need to reclaim any cores this proc might have, even though it's
			 * not running yet. */
			__proc_take_allcores(p, NULL, NULL, NULL, NULL);
			// fallthrough
		case PROC_RUNNABLE_S:
			// Think about other lists, like WAITING, or better ways to do this
			deschedule_proc(p);
			break;
		case PROC_RUNNING_S:
			#if 0
			// here's how to do it manually
			if (current == p) {
				lcr3(boot_cr3);
				proc_decref(p);		/* this decref is for the cr3 */
				current = NULL;
			}
			#endif
			send_kernel_message(get_pcoreid(p, 0), __death, 0, 0, 0,
			                    KMSG_ROUTINE);
			__seq_start_write(&p->procinfo->coremap_seqctr);
			// TODO: might need to sort num_vcores too later (VC#)
			/* vcore is unmapped on the receive side */
			__seq_end_write(&p->procinfo->coremap_seqctr);
			#if 0
			/* right now, RUNNING_S only runs on a mgmt core (0), not cores
			 * managed by the idlecoremap.  so don't do this yet. */
			put_idle_core(get_pcoreid(p, 0));
			#endif
			break;
		case PROC_RUNNING_M:
			/* Send the DEATH message to every core running this process, and
			 * deallocate the cores.
			 * The rule is that the vcoremap is set before proc_run, and reset
			 * within proc_destroy */
			__proc_take_allcores(p, __death, (void *SNT)0, (void *SNT)0,
			                     (void *SNT)0);
			break;
		case PROC_CREATED:
			break;
		default:
			panic("Weird state(%s) in %s()", procstate2str(p->state),
			      __FUNCTION__);
	}
	__proc_set_state(p, PROC_DYING);
	/* This prevents processes from accessing their old files while dying, and
	 * will help if these files (or similar objects in the future) hold
	 * references to p (preventing a __proc_free()). */
	close_all_files(&p->open_files, FALSE);
	/* This decref is for the process's existence. */
	proc_decref(p);
	/* Unlock and possible decref and wait.  A death IPI should be on its way,
	 * either from the RUNNING_S one, or from proc_take_cores with a __death.
	 * in general, interrupts should be on when you call proc_destroy locally,
	 * but currently aren't for all things (like traphandlers). */
	spin_unlock(&p->proc_lock);
	/* at this point, we normally have one ref to be eaten in kmsg_pending and
	 * one for every 'current'.  and maybe one for a parent */
	__proc_kmsg_pending(p, self_ipi_pending);
	return;
}

/* Helper function.  Starting from prev, it will find the next free vcoreid,
 * which is the next vcore that is not valid.
 * You better hold the lock before calling this. */
static uint32_t get_free_vcoreid(struct proc *SAFE p, uint32_t prev)
{
	uint32_t i;
	for (i = prev; i < MAX_NUM_CPUS; i++)
		if (!p->procinfo->vcoremap[i].valid)
			break;
	if (i + 1 >= MAX_NUM_CPUS)
		warn("At the end of the vcorelist.  Might want to check that out.");
	return i;
}

/* Helper function.  Starting from prev, it will find the next busy vcoreid,
 * which is the next vcore that is valid.
 * You better hold the lock before calling this. */
static uint32_t get_busy_vcoreid(struct proc *SAFE p, uint32_t prev)
{
	uint32_t i;
	for (i = prev; i < MAX_NUM_CPUS; i++)
		if (p->procinfo->vcoremap[i].valid)
			break;
	if (i + 1 >= MAX_NUM_CPUS)
		warn("At the end of the vcorelist.  Might want to check that out.");
	return i;
}

/* Helper function.  Is the given pcore a mapped vcore?  No locking involved, be
 * careful. */
static bool is_mapped_vcore(struct proc *p, uint32_t pcoreid)
{
	return p->procinfo->pcoremap[pcoreid].valid;
}

/* Helper function.  Find the vcoreid for a given physical core id for proc p.
 * No locking involved, be careful.  Panics on failure. */
static uint32_t get_vcoreid(struct proc *p, uint32_t pcoreid)
{
	assert(is_mapped_vcore(p, pcoreid));
	return p->procinfo->pcoremap[pcoreid].vcoreid;
}

/* Helper function.  Find the pcoreid for a given virtual core id for proc p.
 * No locking involved, be careful.  Panics on failure. */
static uint32_t get_pcoreid(struct proc *p, uint32_t vcoreid)
{
	assert(p->procinfo->vcoremap[vcoreid].valid);
	return p->procinfo->vcoremap[vcoreid].pcoreid;
}

/* Helper function: yields / wraps up current_tf and schedules the _S */
void __proc_yield_s(struct proc *p, struct trapframe *tf)
{
	assert(p->state == PROC_RUNNING_S);
	p->env_tf= *tf;
	env_push_ancillary_state(p);			/* TODO: (HSS) */
	__proc_set_state(p, PROC_RUNNABLE_S);
	schedule_proc(p);
}

/* Yields the calling core.  Must be called locally (not async) for now.
 * - If RUNNING_S, you just give up your time slice and will eventually return.
 * - If RUNNING_M, you give up the current vcore (which never returns), and
 *   adjust the amount of cores wanted/granted.
 * - If you have only one vcore, you switch to RUNNABLE_M.  When you run again,
 *   you'll have one guaranteed core, starting from the entry point.
 *
 * - RES_CORES amt_wanted will be the amount running after taking away the
 *   yielder, unless there are none left, in which case it will be 1.
 *
 * If the call is being nice, it means that it is in response to a preemption
 * (which needs to be checked).  If there is no preemption pending, just return.
 * No matter what, don't adjust the number of cores wanted.
 *
 * This usually does not return (abandon_core()), so it will eat your reference.
 * */
void proc_yield(struct proc *SAFE p, bool being_nice)
{
	uint32_t vcoreid = get_vcoreid(p, core_id());
	struct vcore *vc = &p->procinfo->vcoremap[vcoreid];

	/* no reason to be nice, return */
	if (being_nice && !vc->preempt_pending)
		return;

	spin_lock(&p->proc_lock); /* horrible scalability.  =( */

	/* fate is sealed, return and take the preempt message on the way out.
	 * we're making this check while holding the lock, since the preemptor
	 * should hold the lock when sending messages. */
	if (vc->preempt_served) {
		spin_unlock(&p->proc_lock);
		return;
	}
	/* no need to preempt later, since we are yielding (nice or otherwise) */
	if (vc->preempt_pending)
		vc->preempt_pending = 0;

	switch (p->state) {
		case (PROC_RUNNING_S):
			__proc_yield_s(p, current_tf);	/* current_tf 0'd in abandon core */
			break;
		case (PROC_RUNNING_M):
			printd("[K] Process %d (%p) is yielding on vcore %d\n", p->pid, p,
			       get_vcoreid(p, core_id()));
			/* TODO: (RMS) the Scheduler cannot handle the Runnable Ms (RMS), so
			 * don't yield the last vcore. */
			if (p->procinfo->num_vcores == 1) {
				spin_unlock(&p->proc_lock);
				return;
			}
			__seq_start_write(&p->procinfo->coremap_seqctr);
			// give up core
			__unmap_vcore(p, get_vcoreid(p, core_id()));
			p->resources[RES_CORES].amt_granted = --(p->procinfo->num_vcores);
			if (!being_nice)
				p->resources[RES_CORES].amt_wanted = p->procinfo->num_vcores;
			__seq_end_write(&p->procinfo->coremap_seqctr);
			// add to idle list
			put_idle_core(core_id());
			// last vcore?  then we really want 1, and to yield the gang
			// TODO: (RMS) will actually do this.
			if (p->procinfo->num_vcores == 0) {
				p->resources[RES_CORES].amt_wanted = 1;
				__proc_set_state(p, PROC_RUNNABLE_M);
				schedule_proc(p);
			}
			break;
		case (PROC_DYING):
			/* just return and take the death message (which should be otw) */
			spin_unlock(&p->proc_lock);
			return;
		default:
			// there are races that can lead to this (async death, preempt, etc)
			panic("Weird state(%s) in %s()", procstate2str(p->state),
			      __FUNCTION__);
	}
	spin_unlock(&p->proc_lock);
	proc_decref(p);			/* need to eat the ref passed in */
	/* TODO: (RMS) If there was a change to the idle cores, try and give our
	 * core to someone who was preempted. */
	/* Clean up the core and idle.  For mgmt cores, they will ultimately call
	 * manager, which will call schedule() and will repick the yielding proc. */
	abandon_core();
	smp_idle();
}

/* Sends a notification (aka active notification, aka IPI) to p's vcore.  We
 * only send a notification if one isn't already pending and they are enabled.
 * There's a bunch of weird cases with this, and how pending / enabled are
 * signals between the user and kernel - check the documentation.
 *
 * If you expect to notify yourself, cleanup state and process_routine_kmsg() */
void proc_notify(struct proc *p, uint32_t vcoreid)
{
	struct preempt_data *vcpd = &p->procdata->vcore_preempt_data[vcoreid];
	/* TODO: Currently, there is a race for notif_pending, and multiple senders
	 * can send an IPI.  Worst thing is that the process gets interrupted
	 * briefly and the kernel immediately returns back once it realizes notifs
	 * are masked.  To fix it, we'll need atomic_swapb() (right answer), or not
	 * use a bool. (wrong answer). */
	if (!vcpd->notif_pending) {
		vcpd->notif_pending = TRUE;
		if (vcpd->notif_enabled) {
			/* GIANT WARNING: we aren't using the proc-lock to protect the
			 * vcoremap.  We want to be able to use this from interrupt context,
			 * and don't want the proc_lock to be an irqsave. */
			if ((p->state & PROC_RUNNING_M) && // TODO: (VC#) (_S state)
			              (p->procinfo->vcoremap[vcoreid].valid)) {
				printd("[kernel] sending notif to vcore %d\n", vcoreid);
				send_kernel_message(get_pcoreid(p, vcoreid), __notify, p, 0, 0,
				                    KMSG_ROUTINE);
			} else { // TODO: think about this, fallback, etc
				warn("Vcore unmapped, not receiving an active notif");
			}
		}
	}
}

/************************  Preemption Functions  ******************************
 * Don't rely on these much - I'll be sure to change them up a bit.
 *
 * Careful about what takes a vcoreid and what takes a pcoreid.  Also, there may
 * be weird glitches with setting the state to RUNNABLE_M.  It is somewhat in
 * flux.  The num_vcores is changed after take_cores, but some of the messages
 * (or local traps) may not yet be ready to handle seeing their future state.
 * But they should be, so fix those when they pop up.
 *
 * TODO: (RMS) we need to actually make the scheduler handle RUNNABLE_Ms and
 * then schedule these, or change proc_destroy to not assume they need to be
 * descheduled.
 *
 * Another thing to do would be to make the _core functions take a pcorelist,
 * and not just one pcoreid. */

/* Sets a preempt_pending warning for p's vcore, to go off 'when'.  If you care
 * about locking, do it before calling.  Takes a vcoreid! */
void __proc_preempt_warn(struct proc *p, uint32_t vcoreid, uint64_t when)
{
	struct event_msg local_msg = {0};
	/* danger with doing this unlocked: preempt_pending is set, but never 0'd,
	 * since it is unmapped and not dealt with (TODO)*/
	p->procinfo->vcoremap[vcoreid].preempt_pending = when;

	/* Send the event (which internally checks to see how they want it) */
	local_msg.ev_type = EV_PREEMPT_PENDING;
	local_msg.ev_arg1 = vcoreid;
	send_kernel_event(p, &local_msg, vcoreid);

	/* TODO: consider putting in some lookup place for the alarm to find it.
	 * til then, it'll have to scan the vcoremap (O(n) instead of O(m)) */
}

/* Warns all active vcores of an impending preemption.  Hold the lock if you
 * care about the mapping (and you should). */
void __proc_preempt_warnall(struct proc *p, uint64_t when)
{
	uint32_t active_vcoreid = 0;
	for (int i = 0; i < p->procinfo->num_vcores; i++) {
		active_vcoreid = get_busy_vcoreid(p, active_vcoreid);
		__proc_preempt_warn(p, active_vcoreid, when);
		active_vcoreid++;
	}
	/* TODO: consider putting in some lookup place for the alarm to find it.
	 * til then, it'll have to scan the vcoremap (O(n) instead of O(m)) */
}

// TODO: function to set an alarm, if none is outstanding

/* Raw function to preempt a single core.  Returns TRUE if the calling core will
 * get a kmsg.  If you care about locking, do it before calling. */
bool __proc_preempt_core(struct proc *p, uint32_t pcoreid)
{
	uint32_t vcoreid = get_vcoreid(p, pcoreid);

	p->procinfo->vcoremap[vcoreid].preempt_served = TRUE;
	// expects a pcorelist.  assumes pcore is mapped and running_m
	return __proc_take_cores(p, &pcoreid, 1, __preempt, p, 0, 0);
}

/* Raw function to preempt every vcore.  Returns TRUE if the calling core will
 * get a kmsg.  If you care about locking, do it before calling. */
bool __proc_preempt_all(struct proc *p)
{
	/* instead of doing this, we could just preempt_served all possible vcores,
	 * and not just the active ones.  We would need to sort out a way to deal
	 * with stale preempt_serveds first.  This might be just as fast anyways. */
	uint32_t active_vcoreid = 0;
	for (int i = 0; i < p->procinfo->num_vcores; i++) {
		active_vcoreid = get_busy_vcoreid(p, active_vcoreid);
		p->procinfo->vcoremap[active_vcoreid].preempt_served = TRUE;
		active_vcoreid++;
	}
	return __proc_take_allcores(p, __preempt, p, 0, 0);
}

/* Warns and preempts a vcore from p.  No delaying / alarming, or anything.  The
 * warning will be for u usec from now. */
void proc_preempt_core(struct proc *p, uint32_t pcoreid, uint64_t usec)
{
	bool self_ipi_pending = FALSE;
	uint64_t warn_time = read_tsc() + usec * 1000000 / system_timing.tsc_freq;

	/* DYING could be okay */
	if (p->state != PROC_RUNNING_M) {
		warn("Tried to preempt from a non RUNNING_M proc!");
		return;
	}
	spin_lock(&p->proc_lock);
	if (is_mapped_vcore(p, pcoreid)) {
		__proc_preempt_warn(p, get_vcoreid(p, pcoreid), warn_time);
		self_ipi_pending = __proc_preempt_core(p, pcoreid);
	} else {
		warn("Pcore doesn't belong to the process!!");
	}
	/* TODO: (RMS) do this once a scheduler can handle RUNNABLE_M, and make sure
	 * to schedule it */
	#if 0
	if (!p->procinfo->num_vcores) {
		__proc_set_state(p, PROC_RUNNABLE_M);
		schedule_proc(p);
	}
	#endif
	spin_unlock(&p->proc_lock);
	__proc_kmsg_pending(p, self_ipi_pending);
}

/* Warns and preempts all from p.  No delaying / alarming, or anything.  The
 * warning will be for u usec from now. */
void proc_preempt_all(struct proc *p, uint64_t usec)
{
	bool self_ipi_pending = FALSE;
	uint64_t warn_time = read_tsc() + usec * 1000000 / system_timing.tsc_freq;

	spin_lock(&p->proc_lock);
	/* DYING could be okay */
	if (p->state != PROC_RUNNING_M) {
		warn("Tried to preempt from a non RUNNING_M proc!");
		spin_unlock(&p->proc_lock);
		return;
	}
	__proc_preempt_warnall(p, warn_time);
	self_ipi_pending = __proc_preempt_all(p);
	assert(!p->procinfo->num_vcores);
	/* TODO: (RMS) do this once a scheduler can handle RUNNABLE_M, and make sure
	 * to schedule it */
	#if 0
	__proc_set_state(p, PROC_RUNNABLE_M);
	schedule_proc(p);
	#endif
	spin_unlock(&p->proc_lock);
	__proc_kmsg_pending(p, self_ipi_pending);
}

/* Give the specific pcore to proc p.  Lots of assumptions, so don't really use
 * this.  The proc needs to be _M and prepared for it.  the pcore needs to be
 * free, etc. */
void proc_give(struct proc *p, uint32_t pcoreid)
{
	bool self_ipi_pending = FALSE;

	spin_lock(&p->proc_lock);
	// expects a pcorelist, we give it a list of one
	self_ipi_pending = __proc_give_cores(p, &pcoreid, 1);
	spin_unlock(&p->proc_lock);
	__proc_kmsg_pending(p, self_ipi_pending);
}

/* Global version of the helper, for sys_get_vcoreid (might phase that syscall
 * out). */
uint32_t proc_get_vcoreid(struct proc *SAFE p, uint32_t pcoreid)
{
	uint32_t vcoreid;
	// TODO: the code currently doesn't track the vcoreid properly for _S (VC#)
	spin_lock(&p->proc_lock);
	switch (p->state) {
		case PROC_RUNNING_S:
			spin_unlock(&p->proc_lock);
			return 0; // TODO: here's the ugly part
		case PROC_RUNNING_M:
			vcoreid = get_vcoreid(p, pcoreid);
			spin_unlock(&p->proc_lock);
			return vcoreid;
		case PROC_DYING: // death message is on the way
			spin_unlock(&p->proc_lock);
			return 0;
		default:
			spin_unlock(&p->proc_lock);
			panic("Weird state(%s) in %s()", procstate2str(p->state),
			      __FUNCTION__);
	}
}

/* Gives process p the additional num cores listed in pcorelist.  You must be
 * RUNNABLE_M or RUNNING_M before calling this.  If you're RUNNING_M, this will
 * startup your new cores at the entry point with their virtual IDs (or restore
 * a preemption).  If you're RUNNABLE_M, you should call proc_run after this so
 * that the process can start to use its cores.
 *
 * If you're *_S, make sure your core0's TF is set (which is done when coming in
 * via arch/trap.c and we are RUNNING_S), change your state, then call this.
 * Then call proc_run().
 *
 * The reason I didn't bring the _S cases from core_request over here is so we
 * can keep this family of calls dealing with only *_Ms, to avoiding caring if
 * this is called from another core, and to avoid the need_to_idle business.
 * The other way would be to have this function have the side effect of changing
 * state, and finding another way to do the need_to_idle.
 *
 * The returned bool signals whether or not a stack-crushing IPI will come in
 * once you unlock after this function.
 *
 * WARNING: You must hold the proc_lock before calling this! */
bool __proc_give_cores(struct proc *SAFE p, uint32_t *pcorelist, size_t num)
{ TRUSTEDBLOCK
	bool self_ipi_pending = FALSE;
	uint32_t free_vcoreid = 0;
	switch (p->state) {
		case (PROC_RUNNABLE_S):
		case (PROC_RUNNING_S):
			panic("Don't give cores to a process in a *_S state!\n");
			break;
		case (PROC_DYING):
			panic("Attempted to give cores to a DYING process.\n");
			break;
		case (PROC_RUNNABLE_M):
			// set up vcoremap.  list should be empty, but could be called
			// multiple times before proc_running (someone changed their mind?)
			if (p->procinfo->num_vcores) {
				printk("[kernel] Yaaaaaarrrrr!  Giving extra cores, are we?\n");
				// debugging: if we aren't packed, then there's a problem
				// somewhere, like someone forgot to take vcores after
				// preempting.
				for (int i = 0; i < p->procinfo->num_vcores; i++)
					assert(p->procinfo->vcoremap[i].valid);
			}
			// add new items to the vcoremap
			__seq_start_write(&p->procinfo->coremap_seqctr);
			for (int i = 0; i < num; i++) {
				// find the next free slot, which should be the next one
				free_vcoreid = get_free_vcoreid(p, free_vcoreid);
				printd("setting vcore %d to pcore %d\n", free_vcoreid,
				       pcorelist[i]);
				__map_vcore(p, free_vcoreid, pcorelist[i]);
				p->procinfo->num_vcores++;
			}
			__seq_end_write(&p->procinfo->coremap_seqctr);
			break;
		case (PROC_RUNNING_M):
			/* Up the refcnt, since num cores are going to start using this
			 * process and have it loaded in their 'current'. */
			proc_incref(p, num);
			__seq_start_write(&p->procinfo->coremap_seqctr);
			for (int i = 0; i < num; i++) {
				free_vcoreid = get_free_vcoreid(p, free_vcoreid);
				printd("setting vcore %d to pcore %d\n", free_vcoreid,
				       pcorelist[i]);
				__map_vcore(p, free_vcoreid, pcorelist[i]);
				p->procinfo->num_vcores++;
				send_kernel_message(pcorelist[i], __startcore, p, 0, 0,
				                    KMSG_ROUTINE);
				if (pcorelist[i] == core_id())
					self_ipi_pending = TRUE;
			}
			__seq_end_write(&p->procinfo->coremap_seqctr);
			break;
		default:
			panic("Weird state(%s) in %s()", procstate2str(p->state),
			      __FUNCTION__);
	}
	p->resources[RES_CORES].amt_granted += num;
	return self_ipi_pending;
}

/* Makes process p's coremap look like pcorelist (add, remove, etc).  Caller
 * needs to know what cores are free after this call (removed, failed, etc).
 * This info will be returned via corelist and *num.  This will send message to
 * any cores that are getting removed.
 *
 * Before implementing this, we should probably think about when this will be
 * used.  Implies preempting for the message.  The more that I think about this,
 * the less I like it.  For now, don't use this, and think hard before
 * implementing it.
 *
 * WARNING: You must hold the proc_lock before calling this! */
bool __proc_set_allcores(struct proc *SAFE p, uint32_t *pcorelist,
                         size_t *num, amr_t message,TV(a0t) arg0,
                         TV(a1t) arg1, TV(a2t) arg2)
{
	panic("Set all cores not implemented.\n");
}

/* Takes from process p the num cores listed in pcorelist, using the given
 * message for the kernel message (__death, __preempt, etc).  Like the others
 * in this function group, bool signals whether or not an IPI is pending.
 *
 * WARNING: You must hold the proc_lock before calling this! */
bool __proc_take_cores(struct proc *SAFE p, uint32_t *pcorelist,
                       size_t num, amr_t message, TV(a0t) arg0,
                       TV(a1t) arg1, TV(a2t) arg2)
{ TRUSTEDBLOCK
	uint32_t vcoreid, pcoreid;
	bool self_ipi_pending = FALSE;
	switch (p->state) {
		case (PROC_RUNNABLE_M):
			assert(!message);
			break;
		case (PROC_RUNNING_M):
			assert(message);
			break;
		default:
			panic("Weird state(%s) in %s()", procstate2str(p->state),
			      __FUNCTION__);
	}
	spin_lock(&idle_lock);
	assert((num <= p->procinfo->num_vcores) &&
	       (num_idlecores + num <= num_cpus));
	spin_unlock(&idle_lock);
	__seq_start_write(&p->procinfo->coremap_seqctr);
	for (int i = 0; i < num; i++) {
		vcoreid = get_vcoreid(p, pcorelist[i]);
		// while ugly, this is done to facilitate merging with take_all_cores
		pcoreid = get_pcoreid(p, vcoreid);
		assert(pcoreid == pcorelist[i]);
		if (message) {
			if (pcoreid == core_id())
				self_ipi_pending = TRUE;
			send_kernel_message(pcoreid, message, arg0, arg1, arg2,
			                    KMSG_ROUTINE);
		} else {
			/* if there was a msg, the vcore is unmapped on the receive side.
			 * o/w, we need to do it here. */
			__unmap_vcore(p, vcoreid);
		}
		// give the pcore back to the idlecoremap
		put_idle_core(pcoreid);
	}
	p->procinfo->num_vcores -= num;
	__seq_end_write(&p->procinfo->coremap_seqctr);
	p->resources[RES_CORES].amt_granted -= num;
	return self_ipi_pending;
}

/* Takes all cores from a process, which must be in an _M state.  Cores are
 * placed back in the idlecoremap.  If there's a message, such as __death or
 * __preempt, it will be sent to the cores.  The bool signals whether or not an
 * IPI is coming in once you unlock.
 *
 * WARNING: You must hold the proc_lock before calling this! */
bool __proc_take_allcores(struct proc *SAFE p, amr_t message,
                          TV(a0t) arg0, TV(a1t) arg1, TV(a2t) arg2)
{
	uint32_t active_vcoreid = 0, pcoreid;
	bool self_ipi_pending = FALSE;
	switch (p->state) {
		case (PROC_RUNNABLE_M):
			assert(!message);
			break;
		case (PROC_RUNNING_M):
			assert(message);
			break;
		default:
			panic("Weird state(%s) in %s()", procstate2str(p->state),
			      __FUNCTION__);
	}
	spin_lock(&idle_lock);
	assert(num_idlecores + p->procinfo->num_vcores <= num_cpus); // sanity
	spin_unlock(&idle_lock);
	__seq_start_write(&p->procinfo->coremap_seqctr);
	for (int i = 0; i < p->procinfo->num_vcores; i++) {
		// find next active vcore
		active_vcoreid = get_busy_vcoreid(p, active_vcoreid);
		pcoreid = get_pcoreid(p, active_vcoreid);
		if (message) {
			if (pcoreid == core_id())
				self_ipi_pending = TRUE;
			send_kernel_message(pcoreid, message, arg0, arg1, arg2,
			                    KMSG_ROUTINE);
		} else {
			/* if there was a msg, the vcore is unmapped on the receive side.
			 * o/w, we need to do it here. */
			__unmap_vcore(p, active_vcoreid);
		}
		// give the pcore back to the idlecoremap
		put_idle_core(pcoreid);
		active_vcoreid++; // for the next loop, skip the one we just used
	}
	p->procinfo->num_vcores = 0;
	__seq_end_write(&p->procinfo->coremap_seqctr);
	p->resources[RES_CORES].amt_granted = 0;
	return self_ipi_pending;
}

/* Helper, to be used when a proc management kmsg should be on its way.  This
 * used to also unlock and then handle the message, back when the proc_lock was
 * an irqsave, and we had an IPI pending.  Now we use routine kmsgs.  If a msg
 * is pending, this needs to decref (to eat the reference of the caller) and
 * then process the message.  Unlock before calling this, since you might not
 * return.
 *
 * There should already be a kmsg waiting for us, since when we checked state to
 * see a message was coming, the message had already been sent before unlocking.
 * Note we do not need interrupts enabled for this to work (you can receive a
 * message before its IPI by polling), though in most cases they will be.
 *
 * TODO: consider inlining this, so __FUNCTION__ works (will require effort in
 * core_request(). */
void __proc_kmsg_pending(struct proc *p, bool ipi_pending)
{
	if (ipi_pending) {
		proc_decref(p);
		process_routine_kmsg(0);
		panic("stack-killing kmsg not found in %s!!!", __FUNCTION__);
	}
}

/* Helper to do the vcore->pcore and inverse mapping.  Hold the lock when
 * calling. */
void __map_vcore(struct proc *p, uint32_t vcoreid, uint32_t pcoreid)
{
	p->procinfo->vcoremap[vcoreid].pcoreid = pcoreid;
	p->procinfo->vcoremap[vcoreid].valid = TRUE;
	p->procinfo->pcoremap[pcoreid].vcoreid = vcoreid;
	p->procinfo->pcoremap[pcoreid].valid = TRUE;
}

/* Helper to unmap the vcore->pcore and inverse mapping.  Hold the lock when
 * calling. */
void __unmap_vcore(struct proc *p, uint32_t vcoreid)
{
	p->procinfo->vcoremap[vcoreid].valid = FALSE;
	p->procinfo->pcoremap[p->procinfo->vcoremap[vcoreid].pcoreid].valid = FALSE;
}

/* Stop running whatever context is on this core, load a known-good cr3, and
 * 'idle'.  Note this leaves no trace of what was running. This "leaves the
 * process's context. */
void abandon_core(void)
{
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	/* Syscalls that don't return will ultimately call abadon_core(), so we need
	 * to make sure we don't think we are still working on a syscall. */
	pcpui->cur_sysc = 0;
	if (pcpui->cur_proc) {
		pcpui->cur_tf = 0;
		__abandon_core();
	}
}

/* Will send a TLB shootdown message to every vcore in the main address space
 * (aka, all vcores for now).  The message will take the start and end virtual
 * addresses as well, in case we want to be more clever about how much we
 * shootdown and batching our messages.  Should do the sanity about rounding up
 * and down in this function too.
 *
 * Hold the proc_lock before calling this.
 *
 * Would be nice to have a broadcast kmsg at this point.  Note this may send a
 * message to the calling core (interrupting it, possibly while holding the
 * proc_lock).  We don't need to process routine messages since it's an
 * immediate message. */
void __proc_tlbshootdown(struct proc *p, uintptr_t start, uintptr_t end)
{
	uint32_t active_vcoreid = 0;
	switch (p->state) {
		case (PROC_RUNNING_S):
			tlbflush();
			break;
		case (PROC_RUNNING_M):
			/* TODO: (TLB) sanity checks and rounding on the ranges */
			for (int i = 0; i < p->procinfo->num_vcores; i++) {
				/* find next active vcore */
				active_vcoreid = get_busy_vcoreid(p, active_vcoreid);
				send_kernel_message(get_pcoreid(p, active_vcoreid),
				                    __tlbshootdown, (void*)start, (void*)end,
				                    0, KMSG_IMMEDIATE);
				active_vcoreid++; /* next loop, skip the one we just used */
			}
			break;
		case (PROC_DYING):
			/* if it is dying, death messages are already on the way to all
			 * cores, including ours, which will clear the TLB. */
			break;
		default:
			/* will probably get this when we have the short handlers */
			warn("Unexpected case %s in %s", procstate2str(p->state),
			     __FUNCTION__);
	}
}

/* Kernel message handler to start a process's context on this core.  Tightly
 * coupled with proc_run().  Interrupts are disabled. */
void __startcore(trapframe_t *tf, uint32_t srcid, void *a0, void *a1, void *a2)
{
	uint32_t pcoreid = core_id(), vcoreid;
	struct proc *p_to_run = (struct proc *CT(1))a0;
	struct trapframe local_tf;
	struct preempt_data *vcpd;

	assert(p_to_run);
	/* the sender of the amsg increfed, thinking we weren't running current. */
	if (p_to_run == current)
		proc_decref(p_to_run);
	vcoreid = get_vcoreid(p_to_run, pcoreid);
	vcpd = &p_to_run->procdata->vcore_preempt_data[vcoreid];
	printd("[kernel] startcore on physical core %d for process %d's vcore %d\n",
	       pcoreid, p_to_run->pid, vcoreid);

	if (seq_is_locked(vcpd->preempt_tf_valid)) {
		__seq_end_write(&vcpd->preempt_tf_valid); /* mark tf as invalid */
		restore_fp_state(&vcpd->preempt_anc);
		/* notif_pending and enabled means the proc wants to receive the IPI,
		 * but might have missed it.  copy over the tf so they can restart it
		 * later, and give them a fresh vcore. */
		if (vcpd->notif_pending && vcpd->notif_enabled) {
			vcpd->notif_tf = vcpd->preempt_tf; // could memset
			proc_init_trapframe(&local_tf, vcoreid, p_to_run->env_entry,
			                    vcpd->transition_stack);
			if (!vcpd->transition_stack)
				warn("No transition stack!");
			vcpd->notif_enabled = FALSE;
			vcpd->notif_pending = FALSE;
		} else {
			/* copy-in the tf we'll pop, then set all security-related fields */
			local_tf = vcpd->preempt_tf;
			proc_secure_trapframe(&local_tf);
		}
	} else { /* not restarting from a preemption, use a fresh vcore */
		proc_init_trapframe(&local_tf, vcoreid, p_to_run->env_entry,
		                    vcpd->transition_stack);
		/* Disable/mask active notifications for fresh vcores */
		vcpd->notif_enabled = FALSE;
	}
	__proc_startcore(p_to_run, &local_tf); // TODO: (HSS) pass silly state *?
}

/* Bail out if it's the wrong process, or if they no longer want a notif.  Make
 * sure that you are passing in a user tf (otherwise, it's a bug).  Try not to
 * grab locks or write access to anything that isn't per-core in here. */
void __notify(trapframe_t *tf, uint32_t srcid, void *a0, void *a1, void *a2)
{
	struct user_trapframe local_tf;
	struct preempt_data *vcpd;
	uint32_t vcoreid;
	struct proc *p = (struct proc*)a0;

	if (p != current)
		return;
	assert(!in_kernel(tf));
	/* We shouldn't need to lock here, since unmapping happens on the pcore and
	 * mapping would only happen if the vcore was free, which it isn't until
	 * after we unmap. */
	assert(tf == current_tf);
	vcoreid = get_vcoreid(p, core_id());
	vcpd = &p->procdata->vcore_preempt_data[vcoreid];
	printd("received active notification for proc %d's vcore %d on pcore %d\n",
	       p->procinfo->pid, vcoreid, core_id());
	/* sort signals.  notifs are now masked, like an interrupt gate */
	if (!vcpd->notif_enabled)
		return;
	vcpd->notif_enabled = FALSE;
	vcpd->notif_pending = FALSE; // no longer pending - it made it here
	/* save the old tf in the notify slot, build and pop a new one.  Note that
	 * silly state isn't our business for a notification. */
	// TODO: this is assuming the struct user_tf is the same as a regular TF
	vcpd->notif_tf = *tf;
	memset(&local_tf, 0, sizeof(local_tf));
	proc_init_trapframe(&local_tf, vcoreid, p->env_entry,
	                    vcpd->transition_stack);
	__proc_startcore(p, &local_tf);
}

void __preempt(trapframe_t *tf, uint32_t srcid, void *a0, void *a1, void *a2)
{
	struct preempt_data *vcpd;
	uint32_t vcoreid, coreid = core_id();
	struct proc *p = (struct proc*)a0;

	if (p != current)
		panic("__preempt arrived for a process (%p) that was not current (%p)!",
		      p, current);
	assert(!in_kernel(tf));
	/* We shouldn't need to lock here, since unmapping happens on the pcore and
	 * mapping would only happen if the vcore was free, which it isn't until
	 * after we unmap. */
	vcoreid = get_vcoreid(p, coreid);
	p->procinfo->vcoremap[vcoreid].preempt_served = FALSE;
	/* either __preempt or proc_yield() ends the preempt phase. */
	p->procinfo->vcoremap[vcoreid].preempt_pending = 0;
	vcpd = &p->procdata->vcore_preempt_data[vcoreid];
	printd("[kernel] received __preempt for proc %d's vcore %d on pcore %d\n",
	       p->procinfo->pid, vcoreid, core_id());

	/* save the old tf in the preempt slot, save the silly state, and signal the
	 * state is a valid tf.  when it is 'written,' it is valid.  Using the
	 * seq_ctrs so userspace can tell between different valid versions.  If the
	 * TF was already valid, it will panic (if CONFIGed that way). */
	// TODO: this is assuming the struct user_tf is the same as a regular TF
	vcpd->preempt_tf = *tf;
	save_fp_state(&vcpd->preempt_anc);
	__seq_start_write(&vcpd->preempt_tf_valid);
	__unmap_vcore(p, vcoreid);
	abandon_core();
	smp_idle();
}

/* Kernel message handler to clean up the core when a process is dying.
 * Note this leaves no trace of what was running.
 * It's okay if death comes to a core that's already idling and has no current.
 * It could happen if a process decref'd before __proc_startcore could incref. */
void __death(trapframe_t *tf, uint32_t srcid, void *SNT a0, void *SNT a1,
             void *SNT a2)
{
	uint32_t vcoreid, coreid = core_id();
	if (current) {
		vcoreid = get_vcoreid(current, coreid);
		printd("[kernel] death on physical core %d for process %d's vcore %d\n",
		       coreid, current->pid, vcoreid);
		__unmap_vcore(current, vcoreid);
	}
	abandon_core();
	smp_idle();
}

/* Kernel message handler, usually sent IMMEDIATE, to shoot down virtual
 * addresses from a0 to a1. */
void __tlbshootdown(struct trapframe *tf, uint32_t srcid, void *a0, void *a1,
                    void *a2)
{
	/* TODO: (TLB) something more intelligent with the range */
	tlbflush();
}

void print_idlecoremap(void)
{
	spin_lock(&idle_lock);
	printk("There are %d idle cores.\n", num_idlecores);
	for (int i = 0; i < num_idlecores; i++)
		printk("idlecoremap[%d] = %d\n", i, idlecoremap[i]);
	spin_unlock(&idle_lock);
}

void print_allpids(void)
{
	void print_proc_state(void *item)
	{
		struct proc *p = (struct proc*)item;
		assert(p);
		printk("%8d %s\n", p->pid, procstate2str(p->state));
	}
	printk("PID      STATE    \n");
	printk("------------------\n");
	spin_lock(&pid_hash_lock);
	hash_for_each(pid_hash, print_proc_state);
	spin_unlock(&pid_hash_lock);
}

void print_proc_info(pid_t pid)
{
	int j = 0;
	struct proc *p = pid2proc(pid);
	if (!p) {
		printk("Bad PID.\n");
		return;
	}
	spinlock_debug(&p->proc_lock);
	//spin_lock(&p->proc_lock); // No locking!!
	printk("struct proc: %p\n", p);
	printk("PID: %d\n", p->pid);
	printk("PPID: %d\n", p->ppid);
	printk("State: 0x%08x\n", p->state);
	printk("Refcnt: %d\n", atomic_read(&p->p_kref.refcount) - 1);
	printk("Flags: 0x%08x\n", p->env_flags);
	printk("CR3(phys): 0x%08x\n", p->env_cr3);
	printk("Num Vcores: %d\n", p->procinfo->num_vcores);
	printk("Vcoremap:\n");
	for (int i = 0; i < p->procinfo->num_vcores; i++) {
		j = get_busy_vcoreid(p, j);
		printk("\tVcore %d: Pcore %d\n", j, get_pcoreid(p, j));
		j++;
	}
	printk("Resources:\n");
	for (int i = 0; i < MAX_NUM_RESOURCES; i++)
		printk("\tRes type: %02d, amt wanted: %08d, amt granted: %08d\n", i,
		       p->resources[i].amt_wanted, p->resources[i].amt_granted);
	printk("Open Files:\n");
	struct files_struct *files = &p->open_files;
	spin_lock(&files->lock);
	for (int i = 0; i < files->max_files; i++)
		if (files->fd_array[i].fd_file) {
			printk("\tFD: %02d, File: %08p, File name: %s\n", i,
			       files->fd_array[i].fd_file,
			       file_name(files->fd_array[i].fd_file));
		}
	spin_unlock(&files->lock);
	/* No one cares, and it clutters the terminal */
	//printk("Vcore 0's Last Trapframe:\n");
	//print_trapframe(&p->env_tf);
	/* no locking / unlocking or refcnting */
	// spin_unlock(&p->proc_lock);
	proc_decref(p);
}
