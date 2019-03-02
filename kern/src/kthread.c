/* Copyright (c) 2010-13 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Kernel threading.  These are for blocking within the kernel for whatever
 * reason, usually during blocking IO operations. */

#include <kthread.h>
#include <slab.h>
#include <page_alloc.h>
#include <pmap.h>
#include <smp.h>
#include <schedule.h>
#include <kstack.h>
#include <kmalloc.h>
#include <arch/uaccess.h>

#define KSTACK_NR_GUARD_PGS		1
#define KSTACK_GUARD_SZ			(KSTACK_NR_GUARD_PGS * PGSIZE)
static struct kmem_cache *kstack_cache;

/* We allocate KSTKSIZE + PGSIZE vaddrs.  So for one-page stacks, we get two
 * pages.  blob points to the bottom of this space.  Our job is to allocate the
 * physical pages for the stack and set up the virtual-to-physical mappings. */
int kstack_ctor(void *blob, void *priv, int flags)
{
	void *stackbot;

	stackbot = kpages_alloc(KSTKSIZE, flags);
	if (!stackbot)
		return -1;
	if (map_vmap_segment((uintptr_t)blob, 0x123456000, KSTACK_NR_GUARD_PGS,
		                 PTE_NONE))
		goto error;
	if (map_vmap_segment((uintptr_t)blob + KSTACK_GUARD_SZ, PADDR(stackbot),
		                 KSTKSIZE / PGSIZE, PTE_KERN_RW))
		goto error;
	return 0;
error:
	/* On failure, we only need to undo what our dtor would do.  The unmaps
	 * happen in the vmap_arena ffunc. */
	kpages_free(stackbot, KSTKSIZE);
	return -1;
}

/* The vmap_arena free will unmap the vaddrs on its own.  We just need to free
 * the physical memory we allocated in ctor.  Although we still have mappings
 * and TLB entries pointing to the memory after we free it (and thus it can be
 * reused), this is no more dangerous than just freeing the stack.  Errant
 * pointers into an old kstack are still dangerous. */
void kstack_dtor(void *blob, void *priv)
{
	void *stackbot;
	pte_t pte;

	pte = pgdir_walk(boot_pgdir, blob + KSTACK_GUARD_SZ, 0);
	assert(pte_walk_okay(pte));
	stackbot = KADDR(pte_get_paddr(pte));
	kpages_free(stackbot, KSTKSIZE);
}

uintptr_t get_kstack(void)
{
	void *blob;

	blob = kmem_cache_alloc(kstack_cache, MEM_ATOMIC);
	/* TODO: think about MEM_WAIT within kthread/blocking code. */
	assert(blob);
	return (uintptr_t)blob + KSTKSIZE + KSTACK_GUARD_SZ;
}

void put_kstack(uintptr_t stacktop)
{
	kmem_cache_free(kstack_cache, (void*)(stacktop - KSTKSIZE
	                                      - KSTACK_GUARD_SZ));
}

uintptr_t *kstack_bottom_addr(uintptr_t stacktop)
{
	/* canary at the bottom of the stack */
	assert(!PGOFF(stacktop));
	return (uintptr_t*)(stacktop - KSTKSIZE);
}

struct kmem_cache *kthread_kcache;

void kthread_init(void)
{
	kthread_kcache = kmem_cache_create("kthread", sizeof(struct kthread),
					   __alignof__(struct kthread), 0,
					   NULL, 0, 0, NULL);
	kstack_cache = kmem_cache_create("kstack", KSTKSIZE + KSTACK_GUARD_SZ,
	                                 PGSIZE, 0, vmap_arena, kstack_ctor,
									 kstack_dtor, NULL);
}

/* Used by early init routines (smp_boot, etc) */
struct kthread *__kthread_zalloc(void)
{
	struct kthread *kthread;
	kthread = kmem_cache_alloc(kthread_kcache, 0);
	assert(kthread);
	memset(kthread, 0, sizeof(struct kthread));
	return kthread;
}

/* Helper during early boot, where we jump from the bootstack to a real kthread
 * stack, then run f().  Note that we don't have a kthread yet (done in smp.c).
 *
 * After this, our callee (f) can free the bootstack, if we care, by adding it
 * to the base arena (use the KERNBASE addr, not the KERN_LOAD_ADDR). */
void __use_real_kstack(void (*f)(void *arg))
{
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	uintptr_t new_stacktop;

	new_stacktop = get_kstack();
	set_stack_top(new_stacktop);
	__reset_stack_pointer(0, new_stacktop, f);
}

/* Starts kthread on the calling core.  This does not return, and will handle
 * the details of cleaning up whatever is currently running (freeing its stack,
 * etc).  Pairs with sem_down(). */
void restart_kthread(struct kthread *kthread)
{
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	uintptr_t current_stacktop;
	struct kthread *cur_kth;
	struct proc *old_proc;

	/* Avoid messy complications.  The kthread will enable_irqsave() when it
	 * comes back up. */
	disable_irq();
	/* Free any spare, since we need the current to become the spare.  Without
	 * the spare, we can't free our current kthread/stack (we could free the
	 * kthread, but not the stack, since we're still on it).  And we can't free
	 * anything after popping kthread, since we never return. */
	if (pcpui->spare) {
		put_kstack(pcpui->spare->stacktop);
		kmem_cache_free(kthread_kcache, pcpui->spare);
	}
	cur_kth = pcpui->cur_kthread;
	current_stacktop = cur_kth->stacktop;
	assert(!cur_kth->sysc);	/* catch bugs, prev user should clear */
	/* Set the spare stuff (current kthread, which includes its stacktop) */
	pcpui->spare = cur_kth;
	/* When a kthread runs, its stack is the default kernel stack */
	set_stack_top(kthread->stacktop);
	pcpui->cur_kthread = kthread;
	/* Only change current if we need to (the kthread was in process context) */
	if (kthread->proc) {
		if (kthread->proc == pcpui->cur_proc) {
			/* We're already loaded, but we do need to drop the extra ref stored
			 * in kthread->proc. */
			proc_decref(kthread->proc);
			kthread->proc = 0;
		} else {
			/* Load our page tables before potentially decreffing cur_proc.
			 *
			 * We don't need to do an EPT flush here.  The EPT is flushed and
			 * managed in sync with the VMCS.  We won't run a different VM (and
			 * thus *need* a different EPT) without first removing the old GPC,
			 * which ultimately will result in a flushed EPT (on x86, this
			 * actually happens when we clear_owning_proc()). */
			lcr3(kthread->proc->env_cr3);
			/* Might have to clear out an existing current.  If they need to be
			 * set later (like in restartcore), it'll be done on demand. */
			old_proc = pcpui->cur_proc;
			/* Transfer our counted ref from kthread->proc to cur_proc. */
			pcpui->cur_proc = kthread->proc;
			kthread->proc = 0;
			if (old_proc)
				proc_decref(old_proc);
		}
	}
	/* Finally, restart our thread */
	longjmp(&kthread->context, 1);
}

/* Kmsg handler to launch/run a kthread.  This must be a routine message, since
 * it does not return.  */
static void __launch_kthread(uint32_t srcid, long a0, long a1, long a2)
{
	struct kthread *kthread = (struct kthread*)a0;
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	struct proc *cur_proc = pcpui->cur_proc;

	if (pcpui->owning_proc && pcpui->owning_proc != kthread->proc) {
		/* Some process should be running here that is not the same as the
		 * kthread.  This means the _M is getting interrupted or otherwise
		 * delayed.  If we want to do something other than run it (like send the
		 * kmsg to another pcore, or ship the context from here to somewhere
		 * else/deschedule it (like for an _S)), do it here.
		 *
		 * If you want to do something here, call out to the ksched, then
		 * abandon_core(). */
		cmb();	/* do nothing/placeholder */
	}
	/* o/w, just run the kthread.  any trapframes that are supposed to run or
	 * were interrupted will run whenever the kthread smp_idles() or otherwise
	 * finishes. */
	restart_kthread(kthread);
	assert(0);
}

/* Call this when a kthread becomes runnable/unblocked.  We don't do anything
 * particularly smart yet, but when we do, we can put it here. */
void kthread_runnable(struct kthread *kthread)
{
	int dst;

	/* TODO: KSCHED - this is a scheduling decision.  The kthread can be
	 * woken up by threads from somewhat unrelated processes.  Consider
	 * unlocking a sem or kicking an RV from an MCP's syscall.  Where was
	 * this kthread running before?  Did it belong to the MCP?  Is the
	 * kthread from an old MCP that was on this core, but there is now a new
	 * MCP?  (This can happen with alarms, currently).
	 *
	 * For ktasks, they tend to sleep on an RV forever.  Once they migrate
	 * to a core other than core 0 due to blocking on a qlock/sem, they will
	 * tend to stay on that core forever, interfering with an unrelated MCP.
	 *
	 * We could consider some sort of core affinity, but for now, we can
	 * just route all ktasks to core 0.  Note this may hide some bugs that
	 * would otherwise be exposed by running in parallel. */
	if (is_ktask(kthread))
		dst = 0;
	else
		dst = core_id();
	send_kernel_message(dst, __launch_kthread, (long)kthread, 0, 0,
	                    KMSG_ROUTINE);
}

/* Stop the current kthread.  It'll get woken up next time we run routine kmsgs,
 * after all existing kmsgs are processed. */
void kthread_yield(void)
{
	struct semaphore local_sem, *sem = &local_sem;
	sem_init(sem, 0);
	run_as_rkm(sem_up, sem);
	sem_down(sem);
}

void kthread_usleep(uint64_t usec)
{
	ERRSTACK(1);
	/* TODO: classic ksched issue: where do we want the wake up to happen? */
	struct timer_chain *tchain = &per_cpu_info[core_id()].tchain;
	struct rendez rv;

	int ret_zero(void *ignored)
	{
		return 0;
	}

	/* "discard the error" style (we run the conditional code) */
	if (!waserror()) {
		rendez_init(&rv);
		rendez_sleep_timeout(&rv, ret_zero, 0, usec);
	}
	poperror();
}

static void __ktask_wrapper(uint32_t srcid, long a0, long a1, long a2)
{
	ERRSTACK(1);
	void (*fn)(void*) = (void (*)(void*))a0;
	void *arg = (void*)a1;
	char *name = (char*)a2;
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	assert(is_ktask(pcpui->cur_kthread));
	pcpui->cur_kthread->name = name;
	/* There are some rendezs out there that aren't wrapped.  Though no one can
	 * abort them.  Yet. */
	if (waserror()) {
		printk("Ktask %s threw error %s\n", name, current_errstr());
		goto out;
	}
	enable_irq();
	fn(arg);
out:
	disable_irq();
	pcpui->cur_kthread->name = 0;
	poperror();
	/* if we blocked, when we return, PRKM will smp_idle() */
}

/* Creates a kernel task, running fn(arg), named "name".  This is just a routine
 * kernel message that happens to have a name, and is allowed to block.  It
 * won't be associated with any process.  For lack of a better place, we'll just
 * start it on the calling core.  Caller (and/or fn) need to deal with the
 * storage for *name. */
void ktask(char *name, void (*fn)(void*), void *arg)
{
	send_kernel_message(core_id(), __ktask_wrapper, (long)fn, (long)arg,
	                    (long)name, KMSG_ROUTINE);
}

/* Semaphores, using kthreads directly */
static void db_blocked_kth(struct kth_db_info *db);
static void db_unblocked_kth(struct kth_db_info *db);
static void db_init(struct kth_db_info *db, int type);

static void sem_init_common(struct semaphore *sem, int signals)
{
	TAILQ_INIT(&sem->waiters);
	sem->nr_signals = signals;
	db_init(&sem->db, KTH_DB_SEM);
}

void sem_init(struct semaphore *sem, int signals)
{
	sem_init_common(sem, signals);
	spinlock_init(&sem->lock);
}

void sem_init_irqsave(struct semaphore *sem, int signals)
{
	sem_init_common(sem, signals);
	spinlock_init_irqsave(&sem->lock);
}

bool sem_trydown_bulk(struct semaphore *sem, int nr_signals)
{
	bool ret = FALSE;

	/* lockless peek */
	if (sem->nr_signals - nr_signals < 0)
		return ret;
	spin_lock(&sem->lock);
	if (sem->nr_signals - nr_signals >= 0) {
		sem->nr_signals--;
		ret = TRUE;
	}
	spin_unlock(&sem->lock);
	return ret;
}

bool sem_trydown(struct semaphore *sem)
{
	return sem_trydown_bulk(sem, 1);
}

/* Bottom-half of sem_down.  This is called after we jumped to the new stack. */
static void __attribute__((noreturn)) __sem_unlock_and_idle(void *arg)
{
	struct semaphore *sem = (struct semaphore*)arg;

	spin_unlock(&sem->lock);
	smp_idle();
}

static void pre_block_check(int nr_locks)
{
	struct per_cpu_info *pcpui = this_pcpui_ptr();

	assert(can_block(pcpui));
	/* Make sure we aren't holding any locks (only works if SPINLOCK_DEBUG) */
	if (pcpui->lock_depth > nr_locks)
		panic("Kthread tried to sleep, with lockdepth %d\n", pcpui->lock_depth);

}

static struct kthread *save_kthread_ctx(void)
{
	struct kthread *kthread, *new_kthread;
	register uintptr_t new_stacktop;
	struct per_cpu_info *pcpui = this_pcpui_ptr();

	assert(pcpui->cur_kthread);
	/* We're probably going to sleep, so get ready.  We'll check again later. */
	kthread = pcpui->cur_kthread;
	/* We need to have a spare slot for restart, so we also use it when
	 * sleeping.  Right now, we need a new kthread to take over if/when our
	 * current kthread sleeps.  Use the spare, and if not, get a new one.
	 *
	 * Note we do this with interrupts disabled (which protects us from
	 * concurrent modifications). */
	if (pcpui->spare) {
		new_kthread = pcpui->spare;
		new_stacktop = new_kthread->stacktop;
		pcpui->spare = 0;
		/* The old flags could have KTH_IS_KTASK set.  The reason is that the
		 * launching of blocked kthreads also uses PRKM, and that KMSG
		 * (__launch_kthread) doesn't return.  Thus the soon-to-be spare
		 * kthread, that is launching another, has flags & KTH_IS_KTASK set. */
		new_kthread->flags = KTH_DEFAULT_FLAGS;
		new_kthread->proc = 0;
		new_kthread->name = 0;
	} else {
		new_kthread = __kthread_zalloc();
		new_kthread->flags = KTH_DEFAULT_FLAGS;
		new_stacktop = get_kstack();
		new_kthread->stacktop = new_stacktop;
	}
	/* Set the core's new default stack and kthread */
	set_stack_top(new_stacktop);
	pcpui->cur_kthread = new_kthread;
	/* Kthreads that are ktasks are not related to any process, and do not need
	 * to work in a process's address space.  They can operate in any address
	 * space that has the kernel mapped (like boot_pgdir, or any pgdir).  Some
	 * ktasks may switch_to, at which point they do care about the address
	 * space and must maintain a reference.
	 *
	 * Normal kthreads need to stay in the process context, but we want the core
	 * (which could be a vcore) to stay in the context too. */
	if ((kthread->flags & KTH_SAVE_ADDR_SPACE) && current) {
		kthread->proc = current;
		/* In the future, we could check owning_proc. If it isn't set, we could
		 * clear current and transfer the refcnt to kthread->proc.  If so, we'll
		 * need to reset the cr3 to something (boot_cr3 or owning_proc's cr3),
		 * which might not be worth the potentially excessive TLB flush. */
		proc_incref(kthread->proc, 1);
	} else {
		assert(kthread->proc == 0);
	}
	return kthread;
}

static void unsave_kthread_ctx(struct kthread *kthread)
{
	struct per_cpu_info *pcpui = this_pcpui_ptr();
	struct kthread *new_kthread = pcpui->cur_kthread;

	printd("[kernel] Didn't sleep, unwinding...\n");
	/* Restore the core's current and default stacktop */
	if (kthread->flags & KTH_SAVE_ADDR_SPACE) {
		proc_decref(kthread->proc);
		kthread->proc = 0;
	}
	set_stack_top(kthread->stacktop);
	pcpui->cur_kthread = kthread;
	/* Save the allocs as the spare */
	assert(!pcpui->spare);
	pcpui->spare = new_kthread;
}

/* This downs the semaphore and suspends the current kernel context on its
 * waitqueue if there are no pending signals. */
void sem_down(struct semaphore *sem)
{
	bool irqs_were_on = irq_is_enabled();
	struct kthread *kthread;

	pre_block_check(0);

	/* Try to down the semaphore.  If there is a signal there, we can skip all
	 * of the sleep prep and just return. */
#ifdef CONFIG_SEM_SPINWAIT
	for (int i = 0; i < CONFIG_SEM_SPINWAIT_NR_LOOPS; i++) {
		if (sem_trydown(sem))
			goto block_return_path;
		cpu_relax();
	}
#else
	if (sem_trydown(sem))
		goto block_return_path;
#endif

	kthread = save_kthread_ctx();
	if (setjmp(&kthread->context))
		goto block_return_path;

	spin_lock(&sem->lock);
	sem->nr_signals -= 1;
	if (sem->nr_signals < 0) {
		TAILQ_INSERT_TAIL(&sem->waiters, kthread, link);
		db_blocked_kth(&sem->db);
		/* At this point, we know we'll sleep and change stacks.  Once we unlock
		 * the sem, we could have the kthread restarted (possibly on another
		 * core), so we need to leave the old stack before unlocking.  If we
		 * don't and we stay on the stack, then if we take an IRQ or NMI (NMI
		 * that doesn't change stacks, unlike x86_64), we'll be using the stack
		 * at the same time as the kthread.  We could just disable IRQs, but
		 * that wouldn't protect us from NMIs that don't change stacks. */
		__reset_stack_pointer(sem, current_kthread->stacktop,
		                      __sem_unlock_and_idle);
		assert(0);
	}
	spin_unlock(&sem->lock);

	unsave_kthread_ctx(kthread);

block_return_path:
	printd("[kernel] Returning from being 'blocked'! at %llu\n", read_tsc());
	/* restart_kthread and longjmp did not reenable IRQs.  We need to make sure
	 * irqs are on if they were on when we started to block.  If they were
	 * already on and we short-circuited the block, it's harmless to reenable
	 * them. */
	if (irqs_were_on)
		enable_irq();
}

void sem_down_bulk(struct semaphore *sem, int nr_signals)
{
	/* This is far from ideal.  Our current sem code expects a 1:1 pairing of
	 * signals to waiters.  For instance, if we have 10 waiters of -1 each or 1
	 * waiter of -10, we can't tell from looking at the overall structure.  We'd
	 * need to track the desired number of signals per waiter.
	 *
	 * Note that if there are a bunch of signals available, sem_down will
	 * quickly do a try_down and return, so we won't block repeatedly.  But if
	 * we do block, we could wake up N times. */
	for (int i = 0; i < nr_signals; i++)
		sem_down(sem);
}

/* Ups the semaphore.  If it was < 0, we need to wake up someone, which we do.
 * Returns TRUE if we woke someone, FALSE o/w (used for debugging in some
 * places).  If we need more control, we can implement a version of the old
 * __up_sem() again.  */
bool sem_up(struct semaphore *sem)
{
	struct kthread *kthread = 0;

	spin_lock(&sem->lock);
	if (sem->nr_signals++ < 0) {
		assert(!TAILQ_EMPTY(&sem->waiters));
		/* could do something with 'priority' here */
		kthread = TAILQ_FIRST(&sem->waiters);
		TAILQ_REMOVE(&sem->waiters, kthread, link);
		db_unblocked_kth(&sem->db);
	} else {
		assert(TAILQ_EMPTY(&sem->waiters));
	}
	spin_unlock(&sem->lock);
	/* Note that once we call kthread_runnable(), we cannot touch the sem again.
	 * Some sems are on stacks.  The caller can touch sem, if it knows about the
	 * memory/usage of the sem.  Likewise, we can't touch the kthread either. */
	if (kthread) {
		kthread_runnable(kthread);
		return TRUE;
	}
	return FALSE;
}

bool sem_trydown_bulk_irqsave(struct semaphore *sem, int nr_signals)
{
	bool ret;
	int8_t irq_state = 0;

	disable_irqsave(&irq_state);
	ret = sem_trydown_bulk(sem, nr_signals);
	enable_irqsave(&irq_state);
	return ret;
}

bool sem_trydown_irqsave(struct semaphore *sem)
{
	return sem_trydown_bulk_irqsave(sem, 1);
}

void sem_down_bulk_irqsave(struct semaphore *sem, int nr_signals)
{
	int8_t irq_state = 0;

	disable_irqsave(&irq_state);
	sem_down_bulk(sem, nr_signals);
	enable_irqsave(&irq_state);
}

void sem_down_irqsave(struct semaphore *sem)
{
	sem_down_bulk_irqsave(sem, 1);
}

bool sem_up_irqsave(struct semaphore *sem)
{
	bool retval;
	int8_t irq_state = 0;

	disable_irqsave(&irq_state);
	retval = sem_up(sem);
	enable_irqsave(&irq_state);
	return retval;
}

/* Sem debugging */

#ifdef CONFIG_SEMAPHORE_DEBUG

static struct kth_db_tailq objs_with_waiters =
                       TAILQ_HEAD_INITIALIZER(objs_with_waiters);
static spinlock_t objs_with_waiters_lock = SPINLOCK_INITIALIZER_IRQSAVE;

static struct kthread_tailq *db_get_waiters(struct kth_db_info *db)
{
	struct semaphore *sem;
	struct cond_var *cv;

	switch (db->type) {
	case KTH_DB_SEM:
		return &container_of(db, struct semaphore, db)->waiters;
	case KTH_DB_CV:
		return &container_of(db, struct cond_var, db)->waiters;
	}
	panic("Bad type %d in db %p\n", db->type, db);
}

static spinlock_t *db_get_spinlock(struct kth_db_info *db)
{
	struct semaphore *sem;
	struct cond_var *cv;

	switch (db->type) {
	case KTH_DB_SEM:
		return &container_of(db, struct semaphore, db)->lock;
	case KTH_DB_CV:
		return container_of(db, struct cond_var, db)->lock;
	}
	panic("Bad type %d in db %p\n", db->type, db);
}

static void db_blocked_kth(struct kth_db_info *db)
{
	spin_lock_irqsave(&objs_with_waiters_lock);
	if (!db->on_list) {
		TAILQ_INSERT_HEAD(&objs_with_waiters, db, link);
		db->on_list = true;
	}
	spin_unlock_irqsave(&objs_with_waiters_lock);
}

static void db_unblocked_kth(struct kth_db_info *db)
{
	spin_lock_irqsave(&objs_with_waiters_lock);
	if (TAILQ_EMPTY(db_get_waiters(db))) {
		TAILQ_REMOVE(&objs_with_waiters, db, link);
		db->on_list = false;
	}
	spin_unlock_irqsave(&objs_with_waiters_lock);
}

static void db_init(struct kth_db_info *db, int type)
{
	db->type = type;
	db->on_list = false;
}

static bool __obj_has_pid(struct kth_db_info *db, pid_t pid)
{
	struct kthread *kth_i;

	if (pid == -1)
		return true;
	TAILQ_FOREACH(kth_i, db_get_waiters(db), link) {
		if (kth_i->proc) {
			if (kth_i->proc->pid == pid)
				return true;
		} else {
			if (pid == 0)
				return true;
		}
	}
	return false;
}

static void db_print_obj(struct kth_db_info *db, pid_t pid)
{
	struct kthread *kth_i;

	/* Always safe to irqsave.  We trylock, since the lock ordering is
	 * obj_lock
	 * -> list_lock. */
	if (!spin_trylock_irqsave(db_get_spinlock(db)))
		return;
	if (!__obj_has_pid(db, pid)) {
		spin_unlock_irqsave(db_get_spinlock(db));
		return;
	}
	printk("Object %p (%3s):\n", db, db->type == KTH_DB_SEM ? "sem" :
	                                 db->type == KTH_DB_CV ? "cv" : "unk");
	TAILQ_FOREACH(kth_i, db_get_waiters(db), link)
		printk("\tKthread %p (%s), proc %d, sysc %p, pc/frame %p %p\n",
		       kth_i, kth_i->name, kth_i->proc ? kth_i->proc->pid : 0,
		       kth_i->sysc, jmpbuf_get_pc(&kth_i->context),
		       jmpbuf_get_fp(&kth_i->context));
	printk("\n");
	spin_unlock_irqsave(db_get_spinlock(db));
}

void print_db_blk_info(pid_t pid)
{
	struct kth_db_info *db_i;

	print_lock();
	printk("All objects with waiters:\n");
	spin_lock_irqsave(&objs_with_waiters_lock);
	TAILQ_FOREACH(db_i, &objs_with_waiters, link)
		db_print_obj(db_i, pid);
	spin_unlock_irqsave(&objs_with_waiters_lock);
	print_unlock();
}

#else

static void db_blocked_kth(struct kth_db_info *db)
{
}

static void db_unblocked_kth(struct kth_db_info *db)
{
}

static void db_init(struct kth_db_info *db, int type)
{
}

void print_db_blk_info(pid_t pid)
{
	printk("Failed to print all sems: build with CONFIG_SEMAPHORE_DEBUG\n");
}

#endif /* CONFIG_SEMAPHORE_DEBUG */

static void __cv_raw_init(struct cond_var *cv)
{
	TAILQ_INIT(&cv->waiters);
	cv->nr_waiters = 0;
	db_init(&cv->db, KTH_DB_CV);
}

/* Condition variables, using semaphores and kthreads */
void cv_init(struct cond_var *cv)
{
	__cv_raw_init(cv);

	cv->lock = &cv->internal_lock;
	spinlock_init(cv->lock);
}

void cv_init_irqsave(struct cond_var *cv)
{
	__cv_raw_init(cv);

	cv->lock = &cv->internal_lock;
	spinlock_init_irqsave(cv->lock);
}

void cv_init_with_lock(struct cond_var *cv, spinlock_t *lock)
{
	__cv_raw_init(cv);

	cv->lock = lock;
}

void cv_init_irqsave_with_lock(struct cond_var *cv, spinlock_t *lock)
{
	cv_init_with_lock(cv, lock);
}

void cv_lock(struct cond_var *cv)
{
	spin_lock(cv->lock);
}

void cv_unlock(struct cond_var *cv)
{
	spin_unlock(cv->lock);
}

void cv_lock_irqsave(struct cond_var *cv, int8_t *irq_state)
{
	disable_irqsave(irq_state);
	cv_lock(cv);
}

void cv_unlock_irqsave(struct cond_var *cv, int8_t *irq_state)
{
	cv_unlock(cv);
	enable_irqsave(irq_state);
}

static void __attribute__((noreturn)) __cv_unlock_and_idle(void *arg)
{
	struct cond_var *cv = arg;

	cv_unlock(cv);
	smp_idle();
}

/* Comes in locked.  Regarding IRQs, the initial cv_lock_irqsave would have
 * disabled irqs.  When this returns, IRQs would still be disabled.  If it was a
 * regular cv_lock(), IRQs will be enabled when we return. */
void cv_wait_and_unlock(struct cond_var *cv)
{
	bool irqs_were_on = irq_is_enabled();
	struct kthread *kthread;

	pre_block_check(1);

	kthread = save_kthread_ctx();
	if (setjmp(&kthread->context)) {
		/* When the kthread restarts, IRQs are off. */
		if (irqs_were_on)
			enable_irq();
		return;
	}

	TAILQ_INSERT_TAIL(&cv->waiters, kthread, link);
	cv->nr_waiters++;
	db_blocked_kth(&cv->db);

	__reset_stack_pointer(cv, current_kthread->stacktop,
	                      __cv_unlock_and_idle);
	assert(0);
}

/* Comes in locked.  Note cv_lock does not disable irqs.   They should still be
 * disabled from the initial cv_lock_irqsave(), which cv_wait_and_unlock()
 * maintained. */
void cv_wait(struct cond_var *cv)
{
	cv_wait_and_unlock(cv);
	cv_lock(cv);
}

/* Helper, wakes exactly one, and there should have been at least one waiter. */
static void __cv_wake_one(struct cond_var *cv)
{
	struct kthread *kthread;

	kthread = TAILQ_FIRST(&cv->waiters);
	TAILQ_REMOVE(&cv->waiters, kthread, link);
	db_unblocked_kth(&cv->db);
	kthread_runnable(kthread);
}

void __cv_signal(struct cond_var *cv)
{
	if (cv->nr_waiters) {
		cv->nr_waiters--;
		__cv_wake_one(cv);
	}
}

void __cv_broadcast(struct cond_var *cv)
{
	while (cv->nr_waiters) {
		cv->nr_waiters--;
		__cv_wake_one(cv);
	}
}

void cv_signal(struct cond_var *cv)
{
	spin_lock(cv->lock);
	__cv_signal(cv);
	spin_unlock(cv->lock);
}

void cv_broadcast(struct cond_var *cv)
{
	spin_lock(cv->lock);
	__cv_broadcast(cv);
	spin_unlock(cv->lock);
}

void cv_signal_irqsave(struct cond_var *cv, int8_t *irq_state)
{
	disable_irqsave(irq_state);
	cv_signal(cv);
	enable_irqsave(irq_state);
}

void cv_broadcast_irqsave(struct cond_var *cv, int8_t *irq_state)
{
	disable_irqsave(irq_state);
	cv_broadcast(cv);
	enable_irqsave(irq_state);
}

/* Helper, aborts and releases a CLE.  dereg_ spinwaits on abort_in_progress.
 * This can throw a PF */
static void __abort_and_release_cle(struct cv_lookup_elm *cle)
{
	int8_t irq_state = 0;
	/* At this point, we have a handle on the syscall that we want to abort (via
	 * the cle), and we know none of the memory will disappear on us (deregers
	 * wait on the flag).  So we'll signal ABORT, which rendez will pick up next
	 * time it is awake.  Then we make sure it is awake with a broadcast. */
	atomic_or(&cle->sysc->flags, SC_ABORT);
	cmb();	/* flags write before signal; atomic op provided CPU mb */
	cv_broadcast_irqsave(cle->cv, &irq_state);
	cmb();	/* broadcast writes before abort flag; atomic op provided CPU mb */
	atomic_dec(&cle->abort_in_progress);
}

/* Attempts to abort p's sysc.  It will only do so if the sysc lookup succeeds,
 * so we can handle "guesses" for syscalls that might not be sleeping.  This
 * style of "do it if you know you can" is the best way here - anything else
 * runs into situations where you don't know if the memory is safe to touch or
 * not (we're doing a lookup via pointer address, and only dereferencing if that
 * succeeds).  Even something simple like letting userspace write SC_ABORT is
 * very hard for them, since they don't know a sysc's state for sure (under the
 * current system).
 *
 * Here are the rules:
 * - if you're flagged SC_ABORT, you don't sleep
 * - if you sleep, you're on the list
 * - if you are on the list or abort_in_progress is set, CV is signallable, and
 *   all the memory for CLE is safe */
bool abort_sysc(struct proc *p, uintptr_t sysc)
{
	ERRSTACK(1);
	struct cv_lookup_elm *cle;
	int8_t irq_state = 0;

	spin_lock_irqsave(&p->abort_list_lock);
	TAILQ_FOREACH(cle, &p->abortable_sleepers, link) {
		if ((uintptr_t)cle->sysc == sysc) {
			/* Note: we could have multiple aborters, so we need to use a
			 * numeric refcnt instead of a flag. */
			atomic_inc(&cle->abort_in_progress);
			break;
		}
	}
	spin_unlock_irqsave(&p->abort_list_lock);
	if (!cle)
		return FALSE;
	if (!waserror())	/* discard error */
		__abort_and_release_cle(cle);
	poperror();
	return TRUE;
}

/* This will abort any abortables at the time the call was started for which
 * should_abort(cle, arg) returns true.  New abortables could be registered
 * concurrently.
 *
 * One caller for this is proc_destroy(), in which case DYING_ABORT will be set,
 * and new abortables will quickly abort and dereg when they see their proc is
 * DYING_ABORT. */
static int __abort_all_sysc(struct proc *p,
                            bool (*should_abort)(struct cv_lookup_elm*, void*),
                            void *arg)
{
	ERRSTACK(1);
	struct cv_lookup_elm *cle;
	int8_t irq_state = 0;
	struct cv_lookup_tailq abortall_list;
	uintptr_t old_proc = switch_to(p);
	int ret = 0;

	/* Concerns: we need to not remove them from their original list, since
	 * concurrent wake ups will cause a dereg, which will remove from the list.
	 * We also can't touch freed memory, so we need a refcnt to keep cles
	 * around. */
	TAILQ_INIT(&abortall_list);
	spin_lock_irqsave(&p->abort_list_lock);
	TAILQ_FOREACH(cle, &p->abortable_sleepers, link) {
		if (!should_abort(cle, arg))
			continue;
		atomic_inc(&cle->abort_in_progress);
		TAILQ_INSERT_HEAD(&abortall_list, cle, abortall_link);
		ret++;
	}
	spin_unlock_irqsave(&p->abort_list_lock);
	if (!waserror()) { /* discard error */
		TAILQ_FOREACH(cle, &abortall_list, abortall_link)
			__abort_and_release_cle(cle);
	}
	poperror();
	switch_back(p, old_proc);
	return ret;
}

static bool always_abort(struct cv_lookup_elm *cle, void *arg)
{
	return TRUE;
}

void abort_all_sysc(struct proc *p)
{
	__abort_all_sysc(p, always_abort, 0);
}

/* cle->sysc could be a bad pointer.  we can either use copy_from_user (btw,
 * we're already in their addr space) or we can use a waserror in
 * __abort_all_sysc().  Both options are fine.  I went with it here for a couple
 * reasons.  It is only this abort function pointer that accesses sysc, though
 * that could change.  Our syscall aborting isn't plugged into a broader error()
 * handler yet, which means we'd want to poperror instead of nexterror in
 * __abort_all_sysc, and that would required int ret getting a volatile flag. */
static bool sysc_uses_fd(struct cv_lookup_elm *cle, void *fd)
{
	struct syscall local_sysc;
	int err;

	err = copy_from_user(&local_sysc, cle->sysc, sizeof(struct syscall));
	/* Trigger an abort on error */
	if (err)
		return TRUE;
	return syscall_uses_fd(&local_sysc, (int)(long)fd);
}

int abort_all_sysc_fd(struct proc *p, int fd)
{
	return __abort_all_sysc(p, sysc_uses_fd, (void*)(long)fd);
}

/* Being on the abortable list means that the CLE, KTH, SYSC, and CV are valid
 * memory.  The lock ordering is {CV lock, list_lock}.  Callers to this *will*
 * have CV held.  This is done to avoid excessive locking in places like
 * rendez_sleep, which want to check the condition before registering. */
void __reg_abortable_cv(struct cv_lookup_elm *cle, struct cond_var *cv)
{
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	cle->cv = cv;
	cle->kthread = pcpui->cur_kthread;
	/* Could be a ktask.  Can build in support for aborting these later */
	if (is_ktask(cle->kthread)) {
		cle->sysc = 0;
		return;
	}
	cle->sysc = cle->kthread->sysc;
	cle->proc = pcpui->cur_proc;
	atomic_init(&cle->abort_in_progress, 0);
	spin_lock_irqsave(&cle->proc->abort_list_lock);
	TAILQ_INSERT_HEAD(&cle->proc->abortable_sleepers, cle, link);
	spin_unlock_irqsave(&cle->proc->abort_list_lock);
}

/* We're racing with the aborter too, who will hold the flag in cle to protect
 * its ref on our cle.  While the lock ordering is CV, list, callers to this
 * must *not* have the cv lock held.  The reason is this waits on a successful
 * abort_sysc, which is trying to cv_{signal,broadcast}, which could wait on the
 * CV lock.  So if we hold the CV lock, we can deadlock (circular dependency).*/
void dereg_abortable_cv(struct cv_lookup_elm *cle)
{
	if (is_ktask(cle->kthread))
		return;
	assert(cle->proc);
	spin_lock_irqsave(&cle->proc->abort_list_lock);
	TAILQ_REMOVE(&cle->proc->abortable_sleepers, cle, link);
	spin_unlock_irqsave(&cle->proc->abort_list_lock);
	/* If we won the race and yanked it out of the list before abort claimed it,
	 * this will already be FALSE. */
	while (atomic_read(&cle->abort_in_progress))
		cpu_relax();
}

/* Helper to sleepers to know if they should abort or not.  I'll probably extend
 * this with things for ktasks in the future. */
bool should_abort(struct cv_lookup_elm *cle)
{
	struct syscall local_sysc;
	int err;

	if (is_ktask(cle->kthread))
		return FALSE;
	if (cle->proc && (cle->proc->state == PROC_DYING_ABORT))
		return TRUE;
	if (cle->sysc) {
		assert(cle->proc && (cle->proc == current));
		err = copy_from_user(&local_sysc, cle->sysc,
		                     offsetof(struct syscall, flags) +
		                     sizeof(cle->sysc->flags));
		/* just go ahead and abort if there was an error */
		if (err || (atomic_read(&local_sysc.flags) & SC_ABORT))
			return TRUE;
	}
	return FALSE;
}

/* Sometimes the kernel needs to switch out of process context and into a
 * 'process-less' kernel thread.  This is basically a ktask.  We use this mostly
 * when performing file ops as the kernel.  It's nasty, and all uses of this
 * probably should be removed.  (TODO: KFOP). */
uintptr_t switch_to_ktask(void)
{
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	struct kthread *kth = pcpui->cur_kthread;

	if (is_ktask(kth))
		return 0;
	/* We leave the SAVE_ADDR_SPACE flag on.  Now we're basically a ktask that
	 * cares about its addr space, since we need to return to it (not that we're
	 * leaving). */
	kth->flags |= KTH_IS_KTASK;
	return 1;
}

void switch_back_from_ktask(uintptr_t old_ret)
{
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	struct kthread *kth = pcpui->cur_kthread;

	if (old_ret)
		kth->flags &= ~KTH_IS_KTASK;
}
