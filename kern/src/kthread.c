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

/* This downs the semaphore and suspends the current kernel context on its
 * waitqueue if there are no pending signals.  Note that the case where the
 * signal is already there is not optimized. */
void sleep_on(struct semaphore *sem)
{
	volatile bool blocking = TRUE;	/* signal to short circuit when restarting*/
	struct kthread *kthread;
	struct page *page;				/* assumption here that stacks are PGSIZE */
	register uintptr_t new_stacktop;
	int8_t irq_state = 0;
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];

	/* interrups would be messy here */
	disable_irqsave(&irq_state);
	/* Try to down the semaphore.  If there is a signal there, we can skip all
	 * of the sleep prep and just return. */
	spin_lock(&sem->lock);	/* no need for irqsave, since we disabled ints */
	if (sem->nr_signals > 0) {
		sem->nr_signals--;
		spin_unlock(&sem->lock);
		goto block_return_path_np;
	}
	/* we're probably going to sleep, so get ready.  We'll check again later. */
	spin_unlock(&sem->lock);
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
		new_stacktop = (uintptr_t)page2kva(page) + PGSIZE;
	}
	/* This is the stacktop we are currently on and wish to save */
	kthread->stacktop = get_stack_top();
	/* Set the core's new default stack */
	set_stack_top(new_stacktop);
	/* The kthread needs to stay in the process context (if there is one), but
	 * we want the core (which could be a vcore) to stay in the context too.  If
	 * we want to leave, we'll need to do that in smp_idle() or elsewhere in the
	 * code. */
	kthread->proc = current;
	/* kthread tracks the syscall it is working on, which implies errno */
	kthread->sysc = pcpui->cur_sysc;
	pcpui->cur_sysc = 0;				/* this core no longer works on sysc */
	if (kthread->proc)
		proc_incref(kthread->proc, 1);
	/* Save the context, toggle blocking for the reactivation */
	save_kernel_tf(&kthread->context);
	if (!blocking)
		goto block_return_path;
	blocking = FALSE;					/* for when it starts back up */
	/* Down the semaphore.  We need this to be inline.  If we're sleeping, once
	 * we unlock the kthread could be started up again and can return and start
	 * trashing this function's stack, hence the weird control flow. */
	spin_lock(&sem->lock);	/* no need for irqsave, since we disabled ints */
	if (sem->nr_signals-- <= 0)
		TAILQ_INSERT_TAIL(&sem->waiters, kthread, link);
	else								/* we didn't sleep */
		goto unwind_sleep_prep;
	spin_unlock(&sem->lock);
	/* Switch to the core's default stack.  After this, don't use local
	 * variables.  TODO: we shouldn't be using new_stacktop either, can't always
	 * trust the register keyword (AFAIK). */
	/* TODO: we shouldn't do this if new_stacktop is on the same page as cur_tf */
	assert(ROUNDDOWN((uintptr_t)current_tf, PGSIZE) !=
	       kthread->stacktop - PGSIZE);
	set_stack_pointer(new_stacktop);
	smp_idle();
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
block_return_path:
	printd("[kernel] Returning from being 'blocked'! at %llu\n", read_tsc());
block_return_path_np:
	enable_irqsave(&irq_state);
	return;
}

/* Starts kthread on the calling core.  This does not return, and will handle
 * the details of cleaning up whatever is currently running (freeing its stack,
 * etc). */
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
		/* this should never happen now.  it probably only was a concern because
		 * i accidentally free kthread's stacktop (the one i was jumping too) */
		assert(ROUNDDOWN((uintptr_t)current_tf, PGSIZE) !=
		       pcpui->spare->stacktop - PGSIZE);
		/* this should probably have a rounddown, since it's not always the top,
		 * or even always PGSIZE... */
		page_decref(kva2page((void*)pcpui->spare->stacktop - PGSIZE));
		kmem_cache_free(kthread_kcache, pcpui->spare);
	}
	current_stacktop = get_stack_top();
	/* When a kthread runs, its stack is the default kernel stack */
	set_stack_top(kthread->stacktop);
	/* Set the spare stuff (current kthread, current (not kthread) stacktop) */
	pcpui->spare = kthread;
	kthread->stacktop = current_stacktop;
	if (current) {
		/* __launch_kthread() should have abandoned if it was diff */
		assert(current == kthread->proc);
		/* no longer need this ref, current holds it */
		proc_decref(kthread->proc);
	} else {
		/* ref gets transfered (or it was 0 (no ref held)) */
		current = kthread->proc;
		if (kthread->proc)
			lcr3(kthread->proc->env_cr3);
	}
	/* Tell the core which syscall we are running (if any) */
	assert(!pcpui->cur_sysc);	/* catch bugs, prev user should clear */
	pcpui->cur_sysc = kthread->sysc;
	/* Finally, restart our thread */
	pop_kernel_tf(&kthread->context);
}

/* Call this when a kthread becomes runnable/unblocked.  We don't do anything
 * particularly smart yet, but when we do, we can put it here. */
void kthread_runnable(struct kthread *kthread)
{
	/* For lack of anything better, send it to ourselves. (TODO: KSCHED) */
	send_kernel_message(core_id(), __launch_kthread, (void*)kthread, 0, 0,
	                    KMSG_ROUTINE);
}

/* Kmsg handler to launch/run a kthread.  This must be a routine message, since
 * it does not return.  Furthermore, like all routine kmsgs that don't return,
 * this needs to handle the fact that it won't return to the given TF (which is
 * a proc's TF, since this was routine). */
void __launch_kthread(struct trapframe *tf, uint32_t srcid, void *a0, void *a1,
	                  void *a2)
{
	struct kthread *kthread = (struct kthread*)a0;
	struct proc *cur_proc = current;
	/* If there is no proc running, don't worry about not returning. */
	if (cur_proc) {
		/* If we're dying, we have a message incoming that we need to deal with,
		 * so we send this message to ourselves (at the end of the queue).  This
		 * is a bit ghetto, and a lot of this will need work. */
		if (cur_proc->state == PROC_DYING) {
			/* We could fake it and send it manually, but this is fine */
			send_kernel_message(core_id(), __launch_kthread, (void*)kthread,
			                    0, 0, KMSG_ROUTINE);
			return;
		}
		if (cur_proc != kthread->proc) {
			/* we're running the kthread from a different proc.  For now, we
			 * can't be _M, since that would be taking away someone's vcore to
			 * process another process's work. */
			/* Keep in mind this can happen if you yield your core after
			 * submitting work (like sys_block()) that will complete on the
			 * current core, and then someone else gets it.
			 * TODO: This can also happen if we started running another _M
			 * here... */
			if (cur_proc->state != PROC_RUNNING_S) {
				printk("cur_proc: %08p, kthread->proc: %08p\n", cur_proc,
				       kthread->proc);
			}
			assert(cur_proc->state == PROC_RUNNING_S);
			spin_lock(&cur_proc->proc_lock);
			/* Wrap up / yield the current _S proc, which calls schedule_proc */
			__proc_yield_s(cur_proc, tf);
			spin_unlock(&cur_proc->proc_lock);
			abandon_core();
		} else {
			/* possible to get here if there is only one _S proc that blocked */
			//assert(cur_proc->state == PROC_RUNNING_M);
			/* Our proc was current, but also wants an old kthread restarted.
			 * This could happen if we are in the kernel servicing a call, and a
			 * kthread tries to get restarted here on the way out.  cur_tf ought
			 * to be the TF that needs to be restarted when the kernel is done
			 * (regardless of whether or not we rerun kthreads). */
			assert(tf == current_tf);
			/* And just let the kthread restart, abandoning the call path via
			 * proc_restartcore (or however we got here). */
		}
	}
	restart_kthread(kthread);
	assert(0);
}
