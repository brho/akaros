/* Copyright (c) 2010 The Regents of the University of California
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

struct kmem_cache *kthread_kcache;

void kthread_init(void)
{
	kthread_kcache = kmem_cache_create("kthread", sizeof(struct kthread),
	                                   __alignof__(struct kthread), 0, 0, 0);
}

/* Starts kthread on the calling core.  This does not return, and will handle
 * the details of cleaning up whatever is currently running (freeing its stack,
 * etc).  Pairs with sem_down(). */
void restart_kthread(struct kthread *kthread)
{
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	uintptr_t current_stacktop;
	/* Avoid messy complications.  The kthread will enable_irqsave() when it
	 * comes back up. */
	disable_irq();
	/* Free any spare, since we need ours to become the spare (since we can't
	 * free our current kthread *before* popping it, nor can we free the current
	 * stack until we pop to the kthread's stack). */
	if (pcpui->spare) {
		/* assumes the stack is a page, and that stacktop is somewhere in
		 * (pg_bottom, pg_bottom + PGSIZE].  Normally, it ought to be pg_bottom
		 * + PGSIZE (on x86).  kva2page can take any kva, not just a page
		 * aligned addr. */
		page_decref(kva2page((void*)pcpui->spare->stacktop - 1));
		kmem_cache_free(kthread_kcache, pcpui->spare);
	}
	current_stacktop = get_stack_top();
	/* When a kthread runs, its stack is the default kernel stack */
	set_stack_top(kthread->stacktop);
#ifdef __CONFIG_KTHREAD_POISON__
	/* TODO: KTHR-STACK */
	/* Assert and switch to cur stack not in use, kthr stack in use */
	uintptr_t *cur_stack_poison, *kth_stack_poison;
	cur_stack_poison = (uintptr_t*)ROUNDDOWN(current_stacktop - 1, PGSIZE);
	assert(*cur_stack_poison == 0xdeadbeef);
	*cur_stack_poison = 0;
	kth_stack_poison = (uintptr_t*)ROUNDDOWN(kthread->stacktop - 1, PGSIZE);
	assert(!*kth_stack_poison);
	*kth_stack_poison = 0xdeadbeef;
#endif /* __CONFIG_KTHREAD_POISON__ */
	/* Set the spare stuff (current kthread, current (not kthread) stacktop) */
	pcpui->spare = kthread;
	kthread->stacktop = current_stacktop;
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
	/* Tell the core which syscall we are running (if any) */
	assert(!pcpui->cur_sysc);	/* catch bugs, prev user should clear */
	pcpui->cur_sysc = kthread->sysc;
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
		 * else/deschedule it (like for an _S)), do it here. */
		cmb();	/* do nothing/placeholder */
		#if 0
		/* example of something to do (wrap up and schedule an _S).  Note this
		 * might not work perfectly, but is just an example.  One thing to be
		 * careful of is that spin_lock() can't be called if __launch isn't
		 * ROUTINE (which it is right now). */
		if (pcpui->owning_proc->state == PROC_RUNNING_S) {
			spin_lock(&pcpui->owning_proc->proc_lock);
			/* Wrap up / yield the _S proc */
			__proc_set_state(pcpui->owning_proc, PROC_WAITING);
			__proc_save_context_s(pcpui->owning_proc, current_ctx);
			spin_unlock(&pcpui->owning_proc->proc_lock);
			proc_wakeup(p);
			abandon_core();
			/* prob need to clear the owning proc?  this is some old shit, so
			 * don't just uncomment it. */
		}
		#endif
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

/* Semaphores, using kthreads directly */
void sem_init(struct semaphore *sem, int signals)
{
	TAILQ_INIT(&sem->waiters);
	sem->nr_signals = signals;
	spinlock_init(&sem->lock);
	sem->irq_okay = FALSE;
}

void sem_init_irqsave(struct semaphore *sem, int signals)
{
	TAILQ_INIT(&sem->waiters);
	sem->nr_signals = signals;
	spinlock_init_irqsave(&sem->lock);
	sem->irq_okay = TRUE;
}

/* This downs the semaphore and suspends the current kernel context on its
 * waitqueue if there are no pending signals.  Note that the case where the
 * signal is already there is not optimized. */
void sem_down(struct semaphore *sem)
{
	volatile bool blocking = TRUE;	/* signal to short circuit when restarting*/
	struct kthread *kthread;
	struct page *page;				/* assumption here that stacks are PGSIZE */
	register uintptr_t new_stacktop;
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];

	assert(can_block(pcpui));
	/* Make sure we aren't holding any locks (only works if SPINLOCK_DEBUG) */
	assert(!pcpui->lock_depth);
	/* Try to down the semaphore.  If there is a signal there, we can skip all
	 * of the sleep prep and just return. */
	spin_lock(&sem->lock);	/* no need for irqsave, since we disabled ints */
	if (sem->nr_signals > 0) {
		sem->nr_signals--;
		spin_unlock(&sem->lock);
		goto block_return_path;
	}
	spin_unlock(&sem->lock);
	/* We're probably going to sleep, so get ready.  We'll check again later. */
	/* Try to get the spare first.  If there is one, we'll use it (o/w, we'll
	 * get a fresh kthread.  Why we need this is more clear when we try to
	 * restart kthreads.  Having them also ought to cut down on contention.
	 * Note we do this with interrupts disabled (which protects us from
	 * concurrent modifications). */
	if (pcpui->spare) {
		kthread = pcpui->spare;
		/* we're using the spare, so we use the page the spare held */
		new_stacktop = kthread->stacktop;
		pcpui->spare = 0;
	} else {
		kthread = kmem_cache_alloc(kthread_kcache, 0);
		assert(kthread);
		assert(!kpage_alloc(&page));	/* decref'd when the kthread is freed */
#ifdef __CONFIG_KTHREAD_POISON__
		/* TODO: KTHR-STACK don't poison like this */
		*(uintptr_t*)page2kva(page) = 0;
#endif /* __CONFIG_KTHREAD_POISON__ */
		new_stacktop = (uintptr_t)page2kva(page) + PGSIZE;
	}
	/* This is the stacktop we are currently on and wish to save */
	kthread->stacktop = get_stack_top();
	/* Set the core's new default stack */
	set_stack_top(new_stacktop);
#ifdef __CONFIG_KTHREAD_POISON__
	/* Mark the new stack as in-use, and unmark the current kthread */
	/* TODO: KTHR-STACK don't poison like this */
	uintptr_t *new_stack_poison, *kth_stack_poison;
	new_stack_poison = (uintptr_t*)ROUNDDOWN(new_stacktop - 1, PGSIZE);
	assert(!*new_stack_poison);
	*new_stack_poison = 0xdeadbeef;
	kth_stack_poison = (uintptr_t*)ROUNDDOWN(kthread->stacktop - 1, PGSIZE);
	assert(*kth_stack_poison == 0xdeadbeef);
	*kth_stack_poison = 0;
#endif /* __CONFIG_KTHREAD_POISON__ */
	/* The kthread needs to stay in the process context (if there is one), but
	 * we want the core (which could be a vcore) to stay in the context too.  In
	 * the future, we could check owning_proc. If it isn't set, we could leave
	 * the process context and transfer the refcnt to kthread->proc. */
	kthread->proc = current;
	/* kthread tracks the syscall it is working on, which implies errno */
	kthread->sysc = pcpui->cur_sysc;
	pcpui->cur_sysc = 0;				/* this core no longer works on sysc */
	if (kthread->proc)
		proc_incref(kthread->proc, 1);
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
		/* At this point, we know we'll sleep and change stacks later.  Once we
		 * unlock, we could have the kthread restarted (possibly on another
		 * core), so we need to disable irqs until we are on our new stack.
		 * Otherwise, if we take an IRQ, we'll be using our stack while another
		 * core is using it (restarted kthread).  Basically, disabling irqs
		 * allows us to atomically unlock and 'yield'. */
		disable_irq();
	} else {							/* we didn't sleep */
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
	/* Save the allocs as the spare */
	assert(!pcpui->spare);
	pcpui->spare = kthread;
	/* save the "freshly alloc'd" stack/page, not the one we came in on */
	kthread->stacktop = new_stacktop;
#ifdef __CONFIG_KTHREAD_POISON__
	/* TODO: KTHR-STACK don't unpoison like this */
	/* switch back to old stack in use, new one not */
	*new_stack_poison = 0;
	*kth_stack_poison = 0xdeadbeef;
#endif /* __CONFIG_KTHREAD_POISON__ */
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

/* Condition variables, using semaphores and kthreads */
void cv_init(struct cond_var *cv)
{
	sem_init(&cv->sem, 0);
	spinlock_init(&cv->lock);
	cv->nr_waiters = 0;
	cv->irq_okay = FALSE;
}

void cv_init_irqsave(struct cond_var *cv)
{
	sem_init_irqsave(&cv->sem, 0);
	spinlock_init_irqsave(&cv->lock);
	cv->nr_waiters = 0;
	cv->irq_okay = TRUE;
}

void cv_lock(struct cond_var *cv)
{
	spin_lock(&cv->lock);
}

void cv_unlock(struct cond_var *cv)
{
	spin_unlock(&cv->lock);
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
	spin_unlock(&cv->lock);
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

/* Comes in locked.  Note cv_lock does not disable irqs. */
void cv_wait(struct cond_var *cv)
{
	cv_wait_and_unlock(cv);
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
	spin_lock(&cv->lock);
	__cv_signal(cv);
	spin_unlock(&cv->lock);
}

void cv_broadcast(struct cond_var *cv)
{
	spin_lock(&cv->lock);
	__cv_broadcast(cv);
	spin_unlock(&cv->lock);
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
