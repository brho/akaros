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

uintptr_t get_kstack(void)
{
	uintptr_t stackbot;
	if (KSTKSIZE == PGSIZE)
		stackbot = (uintptr_t)kpage_alloc_addr();
	else
		stackbot = (uintptr_t)get_cont_pages(KSTKSHIFT - PGSHIFT, 0);
	assert(stackbot);
	return stackbot + KSTKSIZE;
}

void put_kstack(uintptr_t stacktop)
{
	uintptr_t stackbot = stacktop - KSTKSIZE;
	if (KSTKSIZE == PGSIZE)
		page_decref(kva2page((void*)stackbot));
	else
		free_cont_pages((void*)stackbot, KSTKSHIFT - PGSHIFT);
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
	                                   __alignof__(struct kthread), 0, 0, 0);
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

/* Starts kthread on the calling core.  This does not return, and will handle
 * the details of cleaning up whatever is currently running (freeing its stack,
 * etc).  Pairs with sem_down(). */
void restart_kthread(struct kthread *kthread)
{
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	uintptr_t current_stacktop;
	struct kthread *current_kthread;
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
	current_kthread = pcpui->cur_kthread;
	current_stacktop = current_kthread->stacktop;
	assert(!current_kthread->sysc);	/* catch bugs, prev user should clear */
	/* Set the spare stuff (current kthread, which includes its stacktop) */
	pcpui->spare = current_kthread;
	/* When a kthread runs, its stack is the default kernel stack */
	set_stack_top(kthread->stacktop);
	pcpui->cur_kthread = kthread;
#ifdef CONFIG_KTHREAD_POISON
	/* Assert and switch to cur stack not in use, kthr stack in use */
	uintptr_t *cur_stack_poison, *kth_stack_poison;
	cur_stack_poison = kstack_bottom_addr(current_stacktop);
	assert(*cur_stack_poison == 0xdeadbeef);
	*cur_stack_poison = 0;
	kth_stack_poison = kstack_bottom_addr(kthread->stacktop);
	assert(!*kth_stack_poison);
	*kth_stack_poison = 0xdeadbeef;
#endif /* CONFIG_KTHREAD_POISON */
	/* Only change current if we need to (the kthread was in process context) */
	if (kthread->proc) {
		/* Load our page tables before potentially decreffing cur_proc */
		lcr3(kthread->proc->env_cr3);
		/* Might have to clear out an existing current.  If they need to be set
		 * later (like in restartcore), it'll be done on demand. */
		if (pcpui->cur_proc)
			proc_decref(pcpui->cur_proc);
		/* We also transfer our counted ref from kthread->proc to cur_proc */
		pcpui->cur_proc = kthread->proc;
	}
	/* Finally, restart our thread */
	pop_kernel_ctx(&kthread->context);
}

/* Kmsg handler to launch/run a kthread.  This must be a routine message, since
 * it does not return.  */
static void __launch_kthread(uint32_t srcid, long a0, long a1, long a2)
{
	struct kthread *kthread = (struct kthread*)a0;
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	struct proc *cur_proc = pcpui->cur_proc;
	
	/* Make sure we are a routine kmsg */
	assert(in_early_rkmsg_ctx(pcpui));
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
	 * finishes.  We also need to clear the RKMSG context since we will not
	 * return from restart_kth. */
	clear_rkmsg(pcpui);
	restart_kthread(kthread);
	assert(0);
}

/* Call this when a kthread becomes runnable/unblocked.  We don't do anything
 * particularly smart yet, but when we do, we can put it here. */
void kthread_runnable(struct kthread *kthread)
{
	uint32_t dst = core_id();
	#if 0
	/* turn this block on if you want to test migrating non-core0 kthreads */
	switch (dst) {
		case 0:
			break;
		case 7:
			dst = 2;
			break;
		default:
			dst++;
	}
	#endif
	/* For lack of anything better, send it to ourselves. (TODO: KSCHED) */
	send_kernel_message(dst, __launch_kthread, (long)kthread, 0, 0,
	                    KMSG_ROUTINE);
}

/* Kmsg helper for kthread_yield */
static void __wake_me_up(uint32_t srcid, long a0, long a1, long a2)
{
	struct semaphore *sem = (struct semaphore*)a0;
	assert(sem_up(sem));
}

/* Stop the current kthread.  It'll get woken up next time we run routine kmsgs,
 * after all existing kmsgs are processed. */
void kthread_yield(void)
{
	struct semaphore local_sem, *sem = &local_sem;
	sem_init(sem, 0);
	send_kernel_message(core_id(), __wake_me_up, (long)sem, 0, 0,
	                    KMSG_ROUTINE);
	sem_down(sem);
}

static void __ktask_wrapper(uint32_t srcid, long a0, long a1, long a2)
{
	ERRSTACK(1);
	void (*fn)(void*) = (void (*)(void*))a0;
	void *arg = (void*)a1;
	char *name = (char*)a2;
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	assert(pcpui->cur_kthread->is_ktask);
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

void check_poison(char *msg)
{
#ifdef CONFIG_KTHREAD_POISON
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	assert(pcpui->cur_kthread && pcpui->cur_kthread->stacktop);
	if (*kstack_bottom_addr(pcpui->cur_kthread->stacktop) != 0xdeadbeef) {
		printk("\nBad kthread canary, msg: %s\n", msg);
		panic("");
	}
#endif /* CONFIG_KTHREAD_POISON */
}

/* Semaphores, using kthreads directly */
static void debug_downed_sem(struct semaphore *sem);
static void debug_upped_sem(struct semaphore *sem);

static void sem_init_common(struct semaphore *sem, int signals)
{
	TAILQ_INIT(&sem->waiters);
	sem->nr_signals = signals;
#ifdef CONFIG_SEMAPHORE_DEBUG
	sem->is_on_list = FALSE;
	sem->bt_pc = 0;
	sem->bt_fp = 0;
	sem->calling_core = 0;
#endif
}

void sem_init(struct semaphore *sem, int signals)
{
	sem_init_common(sem, signals);
	spinlock_init(&sem->lock);
	sem->irq_okay = FALSE;
}

void sem_init_irqsave(struct semaphore *sem, int signals)
{
	sem_init_common(sem, signals);
	spinlock_init_irqsave(&sem->lock);
	sem->irq_okay = TRUE;
}

bool sem_trydown(struct semaphore *sem)
{
	bool ret = FALSE;
	spin_lock(&sem->lock);
	if (sem->nr_signals > 0) {
		sem->nr_signals--;
		ret = TRUE;
		debug_downed_sem(sem);
	}
	spin_unlock(&sem->lock);
	return ret;
}

/* This downs the semaphore and suspends the current kernel context on its
 * waitqueue if there are no pending signals.  Note that the case where the
 * signal is already there is not optimized. */
void sem_down(struct semaphore *sem)
{
	volatile bool blocking = TRUE;	/* signal to short circuit when restarting*/
	struct kthread *kthread, *new_kthread;
	register uintptr_t new_stacktop;
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];

	assert(can_block(pcpui));
	/* Make sure we aren't holding any locks (only works if SPINLOCK_DEBUG) */
	assert(!pcpui->lock_depth);
	assert(pcpui->cur_kthread);
	/* Try to down the semaphore.  If there is a signal there, we can skip all
	 * of the sleep prep and just return. */
	if (sem_trydown(sem))
		goto block_return_path;
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
		/* Based on how we set is_ktask (in PRKM), we'll usually have a spare
		 * with is_ktask set, even though the default setting is off.  The
		 * reason is that the launching of blocked kthreads also uses PRKM, and
		 * that KMSG (__launch_kthread) doesn't return.  Thus the soon-to-be
		 * spare kthread, that is launching another, has is_ktask set. */
		new_kthread->is_ktask = FALSE;
		new_kthread->proc = 0;
		new_kthread->name = 0;
	} else {
		new_kthread = __kthread_zalloc();
		new_stacktop = get_kstack();
		new_kthread->stacktop = new_stacktop;
#ifdef CONFIG_KTHREAD_POISON
		*kstack_bottom_addr(new_stacktop) = 0;
#endif /* CONFIG_KTHREAD_POISON */
	}
	/* Set the core's new default stack and kthread */
	set_stack_top(new_stacktop);
	pcpui->cur_kthread = new_kthread;
#ifdef CONFIG_KTHREAD_POISON
	/* Mark the new stack as in-use, and unmark the current kthread */
	uintptr_t *new_stack_poison, *kth_stack_poison;
	new_stack_poison = kstack_bottom_addr(new_stacktop);
	assert(!*new_stack_poison);
	*new_stack_poison = 0xdeadbeef;
	kth_stack_poison = kstack_bottom_addr(kthread->stacktop);
	assert(*kth_stack_poison == 0xdeadbeef);
	*kth_stack_poison = 0;
#endif /* CONFIG_KTHREAD_POISON */
	/* Kthreads that are ktasks are not related to any process, and do not need
	 * to work in a process's address space.  They can operate in any address
	 * space that has the kernel mapped (like boot_pgdir, or any pgdir).
	 *
	 * Other kthreads need to stay in the process context (if there is one), but
	 * we want the core (which could be a vcore) to stay in the context too.  In
	 * the future, we could check owning_proc. If it isn't set, we could leave
	 * the process context and transfer the refcnt to kthread->proc. */
	if (!kthread->is_ktask) {
		kthread->proc = current;
		if (kthread->proc)	/* still could be none, like during init */
			proc_incref(kthread->proc, 1);
	} else {
		kthread->proc = 0;
	} 
	/* Save the context, toggle blocking for the reactivation */
	save_kernel_ctx(&kthread->context);
	if (!blocking)
		goto block_return_path;
	blocking = FALSE;					/* for when it starts back up */
	/* Down the semaphore.  We need this to be inline.  If we're sleeping, once
	 * we unlock the kthread could be started up again and can return and start
	 * trashing this function's stack, hence the weird control flow. */
	spin_lock(&sem->lock);
	if (sem->nr_signals-- <= 0) {
		TAILQ_INSERT_TAIL(&sem->waiters, kthread, link);
		debug_downed_sem(sem);
		/* At this point, we know we'll sleep and change stacks later.  Once we
		 * unlock, we could have the kthread restarted (possibly on another
		 * core), so we need to disable irqs until we are on our new stack.
		 * Otherwise, if we take an IRQ, we'll be using our stack while another
		 * core is using it (restarted kthread).  Basically, disabling irqs
		 * allows us to atomically unlock and 'yield'. */
		disable_irq();
	} else {							/* we didn't sleep */
		debug_downed_sem(sem);
		goto unwind_sleep_prep;
	}
	spin_unlock(&sem->lock);
	/* Switch to the core's default stack.  After this, don't use local
	 * variables.  TODO: we shouldn't be using new_stacktop either, can't always
	 * trust the register keyword (AFAIK). */
	set_stack_pointer(new_stacktop);
	smp_idle();							/* reenables irqs eventually */
	/* smp_idle never returns */
	assert(0);
unwind_sleep_prep:
	/* We get here if we should not sleep on sem (the signal beat the sleep).
	 * Note we are not optimizing for cases where the signal won. */
	spin_unlock(&sem->lock);
	printd("[kernel] Didn't sleep, unwinding...\n");
	/* Restore the core's current and default stacktop */
	current = kthread->proc;			/* arguably unnecessary */
	if (kthread->proc)
		proc_decref(kthread->proc);
	set_stack_top(kthread->stacktop);
	pcpui->cur_kthread = kthread;
	/* Save the allocs as the spare */
	assert(!pcpui->spare);
	pcpui->spare = new_kthread;
#ifdef CONFIG_KTHREAD_POISON
	/* switch back to old stack in use, new one not */
	*new_stack_poison = 0;
	*kth_stack_poison = 0xdeadbeef;
#endif /* CONFIG_KTHREAD_POISON */
block_return_path:
	printd("[kernel] Returning from being 'blocked'! at %llu\n", read_tsc());
	return;
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
	} else {
		assert(TAILQ_EMPTY(&sem->waiters));
	}
	debug_upped_sem(sem);
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

bool sem_trydown_irqsave(struct semaphore *sem, int8_t *irq_state)
{
	bool ret;
	disable_irqsave(irq_state);
	ret = sem_trydown(sem);
	enable_irqsave(irq_state);
	return ret;
}

void sem_down_irqsave(struct semaphore *sem, int8_t *irq_state)
{
	disable_irqsave(irq_state);
	sem_down(sem);
	enable_irqsave(irq_state);
}

bool sem_up_irqsave(struct semaphore *sem, int8_t *irq_state)
{
	bool retval;
	disable_irqsave(irq_state);
	retval = sem_up(sem);
	enable_irqsave(irq_state);
	return retval;
}

/* Sem debugging */

#ifdef CONFIG_SEMAPHORE_DEBUG
struct semaphore_tailq sems_with_waiters =
                       TAILQ_HEAD_INITIALIZER(sems_with_waiters);
spinlock_t sems_with_waiters_lock = SPINLOCK_INITIALIZER_IRQSAVE;

/* this gets called any time we downed the sem, regardless of whether or not we
 * waited */
static void debug_downed_sem(struct semaphore *sem)
{
	sem->bt_pc = read_pc();
	sem->bt_fp = read_bp();
	sem->calling_core = core_id();
	if (TAILQ_EMPTY(&sem->waiters) || sem->is_on_list)
		return;
	spin_lock_irqsave(&sems_with_waiters_lock);
	TAILQ_INSERT_HEAD(&sems_with_waiters, sem, link);
	spin_unlock_irqsave(&sems_with_waiters_lock);
	sem->is_on_list = TRUE;
}

/* Called when a sem is upped.  It may or may not have waiters, and it may or
 * may not be on the list. (we could up several times past 0). */
static void debug_upped_sem(struct semaphore *sem)
{
	if (TAILQ_EMPTY(&sem->waiters) && sem->is_on_list) {
		spin_lock_irqsave(&sems_with_waiters_lock);
		TAILQ_REMOVE(&sems_with_waiters, sem, link);
		spin_unlock_irqsave(&sems_with_waiters_lock);
		sem->is_on_list = FALSE;
	}
}

#else

static void debug_downed_sem(struct semaphore *sem)
{
	/* no debugging */
}

static void debug_upped_sem(struct semaphore *sem)
{
	/* no debugging */
}

#endif /* CONFIG_SEMAPHORE_DEBUG */

void print_sem_info(struct semaphore *sem)
{
	struct kthread *kth_i;
	/* Always safe to irqsave */
	spin_lock_irqsave(&sem->lock);
	printk("Semaphore %p has %d signals (neg = waiters)", sem, sem->nr_signals);
#ifdef CONFIG_SEMAPHORE_DEBUG
	printk(", recently downed on core %d with pc/frame %p %p\n",
	       sem->calling_core, sem->bt_pc, sem->bt_fp);
#else
	printk("\n");
#endif /* CONFIG_SEMAPHORE_DEBUG */
	TAILQ_FOREACH(kth_i, &sem->waiters, link)
		printk("\tKthread %p (%s), proc %d (%p), sysc %p\n", kth_i, kth_i->name,
		       kth_i->proc ? kth_i->proc->pid : 0, kth_i->proc, kth_i->sysc);
	spin_unlock_irqsave(&sem->lock);
}

void print_all_sem_info(void)
{
#ifdef CONFIG_SEMAPHORE_DEBUG
	struct semaphore *sem_i;
	printk("All sems with waiters:\n");
	spin_lock_irqsave(&sems_with_waiters_lock);
	TAILQ_FOREACH(sem_i, &sems_with_waiters, link)
		print_sem_info(sem_i);
	spin_unlock_irqsave(&sems_with_waiters_lock);
#else
	printk("Failed to print all sems: build with CONFIG_SEMAPHORE_DEBUG\n");
#endif
}

/* Condition variables, using semaphores and kthreads */
void cv_init(struct cond_var *cv)
{
	sem_init(&cv->sem, 0);
	cv->lock = &cv->internal_lock;
	spinlock_init(cv->lock);
	cv->nr_waiters = 0;
	cv->irq_okay = FALSE;
}

void cv_init_irqsave(struct cond_var *cv)
{
	sem_init_irqsave(&cv->sem, 0);
	cv->lock = &cv->internal_lock;
	spinlock_init_irqsave(cv->lock);
	cv->nr_waiters = 0;
	cv->irq_okay = TRUE;
}

void cv_init_with_lock(struct cond_var *cv, spinlock_t *lock)
{
	sem_init(&cv->sem, 0);
	cv->nr_waiters = 0;
	cv->lock = lock;
	cv->irq_okay = FALSE;
}

void cv_init_irqsave_with_lock(struct cond_var *cv, spinlock_t *lock)
{
	sem_init_irqsave(&cv->sem, 0);
	cv->nr_waiters = 0;
	cv->lock = lock;
	cv->irq_okay = TRUE;
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

/* Helper to clarify the wait/signalling code */
static int nr_sem_waiters(struct semaphore *sem)
{
	int retval;
	retval = 0 - sem->nr_signals;
	assert(retval >= 0);
	return retval;
}

/* Comes in locked.  Note we don't mess with enabling/disabling irqs.  The
 * initial cv_lock would have disabled irqs (if applicable), and we don't mess
 * with that setting at all. */
void cv_wait_and_unlock(struct cond_var *cv)
{
	unsigned long nr_prev_waiters;
	nr_prev_waiters = cv->nr_waiters++;
	spin_unlock(cv->lock);
	/* Wait til our turn.  This forces an ordering of all waiters such that the
	 * order in which they wait is the order in which they down the sem. */
	while (nr_prev_waiters != nr_sem_waiters(&cv->sem))
		cpu_relax();
	printd("core %d, sees nr_sem_waiters: %d, cv_nr_waiters %d\n",
	       core_id(), nr_sem_waiters(&cv->sem), cv->nr_waiters);
	/* Atomically sleeps and 'unlocks' the next kthread from its busy loop (the
	 * one right above this), when it changes the sems nr_signals/waiters. */
	sem_down(&cv->sem);
}

/* Comes in locked.  Note cv_lock does not disable irqs.   They should still be
 * disabled from the initial cv_lock_irqsave(). */
void cv_wait(struct cond_var *cv)
{
	cv_wait_and_unlock(cv);
	if (cv->irq_okay)
		assert(!irq_is_enabled());
	cv_lock(cv);
}

/* Helper, wakes exactly one, and there should have been at least one waiter. */
static void sem_wake_one(struct semaphore *sem)
{
	struct kthread *kthread;
	/* these locks will be irqsaved if the CV is irqsave (only need the one) */
	spin_lock(&sem->lock);
	assert(sem->nr_signals < 0);
	sem->nr_signals++;
	kthread = TAILQ_FIRST(&sem->waiters);
	TAILQ_REMOVE(&sem->waiters, kthread, link);
	debug_upped_sem(sem);
	spin_unlock(&sem->lock);
	kthread_runnable(kthread);
}

void __cv_signal(struct cond_var *cv)
{
	/* Can't short circuit this stuff.  We need to make sure any waiters that
	 * made it past upping the cv->nr_waiters has also downed the sem.
	 * Otherwise we muck with nr_waiters, which could break the ordering
	 * required by the waiters.  We also need to lock while making this check,
	 * o/w a new waiter can slip in after our while loop. */
	while (cv->nr_waiters != nr_sem_waiters(&cv->sem))
		cpu_relax();
	if (cv->nr_waiters) {
		cv->nr_waiters--;
		sem_wake_one(&cv->sem);
	}
}

void __cv_broadcast(struct cond_var *cv)
{
	while (cv->nr_waiters != nr_sem_waiters(&cv->sem))
		cpu_relax();
	while (cv->nr_waiters) {
		cv->nr_waiters--;
		sem_wake_one(&cv->sem);
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

/* Helper, aborts and releases a CLE.  dereg_ spinwaits on abort_in_progress. */
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
bool abort_sysc(struct proc *p, struct syscall *sysc)
{
	struct cv_lookup_elm *cle;
	int8_t irq_state = 0;
	spin_lock_irqsave(&p->abort_list_lock);
	TAILQ_FOREACH(cle, &p->abortable_sleepers, link) {
		if (cle->sysc == sysc) {
			/* Note: we could have multiple aborters, so we need to use a
			 * numeric refcnt instead of a flag. */
			atomic_inc(&cle->abort_in_progress);
			break;
		}
	}
	spin_unlock_irqsave(&p->abort_list_lock);
	if (!cle)
		return FALSE;
	__abort_and_release_cle(cle);
	return TRUE;
}

/* This will abort any abortabls at the time the call was started.  New
 * abortables could be registered concurrently.  The main caller I see for this
 * is proc_destroy(), so DYING will be set, and new abortables will quickly
 * abort and dereg when they see their proc is DYING. */
void abort_all_sysc(struct proc *p)
{
	struct cv_lookup_elm *cle;
	int8_t irq_state = 0;
	struct cv_lookup_tailq abortall_list;
	struct proc *old_proc = switch_to(p);
	/* Concerns: we need to not remove them from their original list, since
	 * concurrent wake ups will cause a dereg, which will remove from the list.
	 * We also can't touch freed memory, so we need a refcnt to keep cles
	 * around. */
	TAILQ_INIT(&abortall_list);
	spin_lock_irqsave(&p->abort_list_lock);
	TAILQ_FOREACH(cle, &p->abortable_sleepers, link) {
		atomic_inc(&cle->abort_in_progress);
		TAILQ_INSERT_HEAD(&abortall_list, cle, abortall_link);
	}
	spin_unlock_irqsave(&p->abort_list_lock);
	TAILQ_FOREACH(cle, &abortall_list, abortall_link)
		__abort_and_release_cle(cle);
	switch_back(p, old_proc);
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
	if (cle->kthread->is_ktask) {
		cle->sysc = 0;
		return;
	}
	cle->sysc = cle->kthread->sysc;
	assert(cle->sysc);
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
	if (cle->kthread->is_ktask)
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
	if (cle->kthread->is_ktask)
		return FALSE;
	if (cle->proc && (cle->proc->state == PROC_DYING))
		return TRUE;
	if (cle->sysc && (atomic_read(&cle->sysc->flags) & SC_ABORT))
		return TRUE;
	return FALSE;
}
