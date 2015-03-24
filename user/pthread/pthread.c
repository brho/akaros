// Needed for sigmask functions...
#define _GNU_SOURCE

#include <ros/trapframe.h>
#include <pthread.h>
#include <vcore.h>
#include <mcs.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include <parlib.h>
#include <ros/event.h>
#include <arch/atomic.h>
#include <arch/arch.h>
#include <sys/queue.h>
#include <sys/mman.h>
#include <event.h>
#include <ucq.h>

struct pthread_queue ready_queue = TAILQ_HEAD_INITIALIZER(ready_queue);
struct pthread_queue active_queue = TAILQ_HEAD_INITIALIZER(active_queue);
struct mcs_pdr_lock queue_lock;
int threads_ready = 0;
int threads_active = 0;
atomic_t threads_total;
bool can_adjust_vcores = TRUE;
bool need_tls = TRUE;

/* Array of per-vcore structs to manage waiting on syscalls and handling
 * overflow.  Init'd in pth_init(). */
struct sysc_mgmt *sysc_mgmt = 0;

/* Helper / local functions */
static int get_next_pid(void);
static inline void spin_to_sleep(unsigned int spins, unsigned int *spun);

/* Pthread 2LS operations */
void pth_sched_entry(void);
void pth_thread_runnable(struct uthread *uthread);
void pth_thread_paused(struct uthread *uthread);
void pth_thread_blockon_sysc(struct uthread *uthread, void *sysc);
void pth_thread_has_blocked(struct uthread *uthread, int flags);
void pth_thread_refl_fault(struct uthread *uthread, unsigned int trap_nr,
                           unsigned int err, unsigned long aux);
void pth_preempt_pending(void);
void pth_spawn_thread(uintptr_t pc_start, void *data);

/* Event Handlers */
static void pth_handle_syscall(struct event_msg *ev_msg, unsigned int ev_type,
                               void *data);

struct schedule_ops pthread_sched_ops = {
	pth_sched_entry,
	pth_thread_runnable,
	pth_thread_paused,
	pth_thread_blockon_sysc,
	pth_thread_has_blocked,
	pth_thread_refl_fault,
	0, /* pth_preempt_pending, */
	0, /* pth_spawn_thread, */
};

/* Publish our sched_ops, overriding the weak defaults */
struct schedule_ops *sched_ops = &pthread_sched_ops;

/* Static helpers */
static void __pthread_free_stack(struct pthread_tcb *pt);
static int __pthread_allocate_stack(struct pthread_tcb *pt);
static void __pth_yield_cb(struct uthread *uthread, void *junk);

/* Swap the contents of two user contexts (not just their pointers). */
static void swap_user_contexts(struct user_context *c1, struct user_context *c2)
{
	struct user_context temp_ctx;
	temp_ctx = *c1;
	*c1 = *c2;
	*c2 = temp_ctx;
}

/* Prep a pthread to run a signal handler.  The original context of the pthread
 * is saved, and a new context with a new stack is set up to run the signal
 * handler the next time the pthread is run. */
static void __pthread_prep_sighandler(struct pthread_tcb *pthread,
                                      void (*entry)(void),
                                      struct siginfo *info)
{
	struct user_context *ctx;

	pthread->sigdata = alloc_sigdata();
	if (info != NULL)
		pthread->sigdata->info = *info;
	init_user_ctx(&pthread->sigdata->u_ctx,
	              (uintptr_t)entry,
	              (uintptr_t)pthread->sigdata->stack);
	if (pthread->uthread.flags & UTHREAD_SAVED) {
		ctx = &pthread->uthread.u_ctx;
		if (pthread->uthread.flags & UTHREAD_FPSAVED) {
			pthread->sigdata->as = pthread->uthread.as;
			pthread->uthread.flags &= ~UTHREAD_FPSAVED;
		}
	} else {
		assert(current_uthread == &pthread->uthread);
		ctx = &vcpd_of(vcore_id())->uthread_ctx;
		save_fp_state(&pthread->sigdata->as);
	}
	swap_user_contexts(ctx, &pthread->sigdata->u_ctx);
}

/* Restore the context saved as the result of running a signal handler on a
 * pthread. This context will execute the next time the pthread is run. */
static void __pthread_restore_after_sighandler(struct pthread_tcb *pthread)
{
	pthread->uthread.u_ctx = pthread->sigdata->u_ctx;
	pthread->uthread.flags |= UTHREAD_SAVED;
	if (pthread->uthread.u_ctx.type == ROS_HW_CTX) {
		pthread->uthread.as = pthread->sigdata->as;
		pthread->uthread.flags |= UTHREAD_FPSAVED;
	}
	free_sigdata(pthread->sigdata);
	pthread->sigdata = NULL;
}

/* Callback when yielding a pthread after upon completion of a sighandler.  We
 * didn't save the current context on yeild, but that's ok because here we
 * restore the original saved context of the pthread and then treat this like a
 * normal voluntary yield. */
static void __exit_sighandler_cb(struct uthread *uthread, void *junk)
{
	__pthread_restore_after_sighandler((struct pthread_tcb*)uthread);
	__pth_yield_cb(uthread, 0);
}

/* Run a specific sighandler from the top of the sigdata stack. The 'info'
 * struct is prepopulated before the call is triggered as the result of a
 * reflected fault. */
static void __run_sighandler()
{
	struct pthread_tcb *me = pthread_self();
	__sigdelset(&me->sigpending, me->sigdata->info.si_signo);
	trigger_posix_signal(me->sigdata->info.si_signo,
	                     &me->sigdata->info,
	                     &me->sigdata->u_ctx);
	uthread_yield(FALSE, __exit_sighandler_cb, 0);
}

/* Run through all pending sighandlers and trigger them with a NULL info field.
 * These handlers are triggered as the result of a pthread_kill(), and thus
 * don't require individual 'info' structs. */
static void __run_pending_sighandlers()
{
	struct pthread_tcb *me = pthread_self();
	sigset_t andset = me->sigpending & (~me->sigmask);
	for (int i = 1; i < _NSIG; i++) {
		if (__sigismember(&andset, i)) {
			__sigdelset(&me->sigpending, i);
			trigger_posix_signal(i, NULL, &me->sigdata->u_ctx);
		}
	}
	uthread_yield(FALSE, __exit_sighandler_cb, 0);
}

/* If the given signal is unmasked, prep the pthread to run it's signal
 * handler, but don't run it yet. In either case, make the pthread runnable
 * again. Once the signal handler is complete, the original context will be
 * restored and restarted. */
static void __pthread_signal_and_restart(struct pthread_tcb *pthread,
                                          int signo, int code, void *addr)
{
	if (!__sigismember(&pthread->sigmask, signo)) {
		if (pthread->sigdata) {
			printf("Pthread sighandler faulted, signal: %d\n", signo);
			/* uthread.c already copied out the faulting ctx into the uth */
			print_user_context(&pthread->uthread.u_ctx);
			exit(-1);
		}
		struct siginfo info = {0};
		info.si_signo = signo;
		info.si_code = code;
		info.si_addr = addr;
		__pthread_prep_sighandler(pthread, __run_sighandler, &info);
	}
	pth_thread_runnable(&pthread->uthread);
}

/* If there are any pending signals, prep the pthread to run it's signal
 * handler. The next time the pthread is run, it will pop into it's signal
 * handler context instead of its original saved context. Once the signal
 * handler is complete, the original context will be restored and restarted. */
static void __pthread_prep_for_pending_posix_signals(pthread_t pthread)
{
	if (!pthread->sigdata && pthread->sigpending) {
		sigset_t andset = pthread->sigpending & (~pthread->sigmask);
		if (!__sigisemptyset(&andset)) {
			__pthread_prep_sighandler(pthread, __run_pending_sighandlers, NULL);
		}
	}
}

/* Called from vcore entry.  Options usually include restarting whoever was
 * running there before or running a new thread.  Events are handled out of
 * event.c (table of function pointers, stuff like that). */
void __attribute__((noreturn)) pth_sched_entry(void)
{
	uint32_t vcoreid = vcore_id();
	if (current_uthread) {
		/* Prep the pthread to run any pending posix signal handlers registered
         * via pthread_kill once it is restored. */
		__pthread_prep_for_pending_posix_signals((pthread_t)current_uthread);
		/* Run the thread itself */
		run_current_uthread();
		assert(0);
	}
	/* no one currently running, so lets get someone from the ready queue */
	struct pthread_tcb *new_thread = NULL;
	/* Try to get a thread.  If we get one, we'll break out and run it.  If not,
	 * we'll try to yield.  vcore_yield() might return, if we lost a race and
	 * had a new event come in, one that may make us able to get a new_thread */
	do {
		handle_events(vcoreid);
		__check_preempt_pending(vcoreid);
		mcs_pdr_lock(&queue_lock);
		new_thread = TAILQ_FIRST(&ready_queue);
		if (new_thread) {
			TAILQ_REMOVE(&ready_queue, new_thread, next);
			TAILQ_INSERT_TAIL(&active_queue, new_thread, next);
			threads_active++;
			threads_ready--;
			mcs_pdr_unlock(&queue_lock);
			/* If you see what looks like the same uthread running in multiple
			 * places, your list might be jacked up.  Turn this on. */
			printd("[P] got uthread %08p on vc %d state %08p flags %08p\n",
			       new_thread, vcoreid,
			       ((struct uthread*)new_thread)->state,
			       ((struct uthread*)new_thread)->flags);
			break;
		}
		mcs_pdr_unlock(&queue_lock);
		/* no new thread, try to yield */
		printd("[P] No threads, vcore %d is yielding\n", vcore_id());
		/* TODO: you can imagine having something smarter here, like spin for a
		 * bit before yielding (or not at all if you want to be greedy). */
		if (can_adjust_vcores)
			vcore_yield(FALSE);
	} while (1);
	assert(new_thread->state == PTH_RUNNABLE);
	/* Prep the pthread to run any pending posix signal handlers registered
     * via pthread_kill once it is restored. */
	__pthread_prep_for_pending_posix_signals(new_thread);
	/* Run the thread itself */
	run_uthread((struct uthread*)new_thread);
	assert(0);
}

/* Could move this, along with start_routine and arg, into the 2LSs */
static void __pthread_run(void)
{
	struct pthread_tcb *me = pthread_self();
	pthread_exit(me->start_routine(me->arg));
}

/* GIANT WARNING: if you make any changes to this, also change the broadcast
 * wakeups (cond var, barrier, etc) */
void pth_thread_runnable(struct uthread *uthread)
{
	struct pthread_tcb *pthread = (struct pthread_tcb*)uthread;
	/* At this point, the 2LS can see why the thread blocked and was woken up in
	 * the first place (coupling these things together).  On the yield path, the
	 * 2LS was involved and was able to set the state.  Now when we get the
	 * thread back, we can take a look. */
	printd("pthread %08p runnable, state was %d\n", pthread, pthread->state);
	switch (pthread->state) {
		case (PTH_CREATED):
		case (PTH_BLK_YIELDING):
		case (PTH_BLK_JOINING):
		case (PTH_BLK_SYSC):
		case (PTH_BLK_PAUSED):
		case (PTH_BLK_MUTEX):
			/* can do whatever for each of these cases */
			break;
		default:
			printf("Odd state %d for pthread %08p\n", pthread->state, pthread);
	}
	pthread->state = PTH_RUNNABLE;
	/* Insert the newly created thread into the ready queue of threads.
	 * It will be removed from this queue later when vcore_entry() comes up */
	mcs_pdr_lock(&queue_lock);
	/* Again, GIANT WARNING: if you change this, change batch wakeup code */
	TAILQ_INSERT_TAIL(&ready_queue, pthread, next);
	threads_ready++;
	mcs_pdr_unlock(&queue_lock);
	/* Smarter schedulers should look at the num_vcores() and how much work is
	 * going on to make a decision about how many vcores to request. */
	if (can_adjust_vcores)
		vcore_request(threads_ready);
}

/* For some reason not under its control, the uthread stopped running (compared
 * to yield, which was caused by uthread/2LS code).
 *
 * The main case for this is if the vcore was preempted or if the vcore it was
 * running on needed to stop.  You are given a uthread that looks like it took a
 * notif, and had its context/silly state copied out to the uthread struct.
 * (copyout_uthread).  Note that this will be called in the context (TLS) of the
 * vcore that is losing the uthread.  If that vcore is running, it'll be in a
 * preempt-event handling loop (not in your 2LS code).  If this is a big
 * problem, I'll change it. */
void pth_thread_paused(struct uthread *uthread)
{
	struct pthread_tcb *pthread = (struct pthread_tcb*)uthread;
	/* Remove from the active list.  Note that I don't particularly care about
	 * the active list.  We keep it around because it causes bugs and keeps us
	 * honest.  After all, some 2LS may want an active list */
	mcs_pdr_lock(&queue_lock);
	threads_active--;
	TAILQ_REMOVE(&active_queue, pthread, next);
	mcs_pdr_unlock(&queue_lock);
	/* communicate to pth_thread_runnable */
	pthread->state = PTH_BLK_PAUSED;
	/* At this point, you could do something clever, like put it at the front of
	 * the runqueue, see if it was holding a lock, do some accounting, or
	 * whatever. */
	pth_thread_runnable(uthread);
}

/* Restarts a uthread hanging off a syscall.  For the simple pthread case, we
 * just make it runnable and let the main scheduler code handle it. */
static void restart_thread(struct syscall *sysc)
{
	struct uthread *ut_restartee = (struct uthread*)sysc->u_data;
	/* uthread stuff here: */
	assert(ut_restartee);
	assert(((struct pthread_tcb*)ut_restartee)->state == PTH_BLK_SYSC);
	assert(ut_restartee->sysc == sysc);	/* set in uthread.c */
	ut_restartee->sysc = 0;	/* so we don't 'reblock' on this later */
	pth_thread_runnable(ut_restartee);
}

/* This handler is usually run in vcore context, though I can imagine it being
 * called by a uthread in some other threading library. */
static void pth_handle_syscall(struct event_msg *ev_msg, unsigned int ev_type,
                               void *data)
{
	struct syscall *sysc;
	assert(in_vcore_context());
	/* if we just got a bit (not a msg), it should be because the process is
	 * still an SCP and hasn't started using the MCP ev_q yet (using the simple
	 * ev_q and glibc's blockon) or because the bit is still set from an old
	 * ev_q (blocking syscalls from before we could enter vcore ctx).  Either
	 * way, just return.  Note that if you screwed up the pth ev_q and made it
	 * NO_MSG, you'll never notice (we used to assert(ev_msg)). */
	if (!ev_msg)
		return;
	/* It's a bug if we don't have a msg (we're handling a syscall bit-event) */
	assert(ev_msg);
	/* Get the sysc from the message and just restart it */
	sysc = ev_msg->ev_arg3;
	assert(sysc);
	restart_thread(sysc);
}

/* This will be called from vcore context, after the current thread has yielded
 * and is trying to block on sysc.  Need to put it somewhere were we can wake it
 * up when the sysc is done.  For now, we'll have the kernel send us an event
 * when the syscall is done. */
void pth_thread_blockon_sysc(struct uthread *uthread, void *syscall)
{
	struct syscall *sysc = (struct syscall*)syscall;
	int old_flags;
	uint32_t vcoreid = vcore_id();
	/* rip from the active queue */
	struct pthread_tcb *pthread = (struct pthread_tcb*)uthread;
	pthread->state = PTH_BLK_SYSC;
	mcs_pdr_lock(&queue_lock);
	threads_active--;
	TAILQ_REMOVE(&active_queue, pthread, next);
	mcs_pdr_unlock(&queue_lock);
	/* Set things up so we can wake this thread up later */
	sysc->u_data = uthread;
	/* Register our vcore's syscall ev_q to hear about this syscall. */
	if (!register_evq(sysc, sysc_mgmt[vcoreid].ev_q)) {
		/* Lost the race with the call being done.  The kernel won't send the
		 * event.  Just restart him. */
		restart_thread(sysc);
	}
	/* GIANT WARNING: do not touch the thread after this point. */
}

void pth_thread_has_blocked(struct uthread *uthread, int flags)
{
	struct pthread_tcb *pthread = (struct pthread_tcb*)uthread;
	/* could imagine doing something with the flags.  For now, we just treat all
	 * externally blocked reasons as 'MUTEX'.  Whatever we do here, we are
	 * mostly communicating to our future selves in pth_thread_runnable(), which
	 * gets called by whoever triggered this callback */
	pthread->state = PTH_BLK_MUTEX;
	/* Just for yucks: */
	if (flags == UTH_EXT_BLK_JUSTICE)
		printf("For great justice!\n");
}

static void handle_div_by_zero(struct uthread *uthread, unsigned int err,
                               unsigned long aux)
{
	struct pthread_tcb *pthread = (struct pthread_tcb*)uthread;
	__pthread_signal_and_restart(pthread, SIGFPE, FPE_INTDIV, (void*)aux);
}

static void handle_gp_fault(struct uthread *uthread, unsigned int err,
                            unsigned long aux)
{
	struct pthread_tcb *pthread = (struct pthread_tcb*)uthread;
	__pthread_signal_and_restart(pthread, SIGSEGV, SEGV_ACCERR, (void*)aux);
}

static void handle_page_fault(struct uthread *uthread, unsigned int err,
                              unsigned long aux)
{
	struct pthread_tcb *pthread = (struct pthread_tcb*)uthread;
	if (!(err & PF_VMR_BACKED)) {
		__pthread_signal_and_restart(pthread, SIGSEGV, SEGV_MAPERR, (void*)aux);
	} else {
		/* stitching for the event handler.  sysc -> uth, uth -> sysc */
		uthread->local_sysc.u_data = uthread;
		uthread->sysc = &uthread->local_sysc;
		pthread->state = PTH_BLK_SYSC;
		/* one downside is that we'll never check the return val of the syscall.  if
		 * we errored out, we wouldn't know til we PF'd again, and inspected the old
		 * retval/err and other sysc fields (make sure the PF is on the same addr,
		 * etc).  could run into this issue on truncated files too. */
		syscall_async(&uthread->local_sysc, SYS_populate_va, aux, 1);
		if (!register_evq(&uthread->local_sysc, sysc_mgmt[vcore_id()].ev_q)) {
			/* Lost the race with the call being done.  The kernel won't send the
			 * event.  Just restart him. */
			restart_thread(&uthread->local_sysc);
		}
	}
}

void pth_thread_refl_fault(struct uthread *uthread, unsigned int trap_nr,
                           unsigned int err, unsigned long aux)
{
	struct pthread_tcb *pthread = (struct pthread_tcb*)uthread;
	pthread->state = PTH_BLK_SYSC;
	mcs_pdr_lock(&queue_lock);
	threads_active--;
	TAILQ_REMOVE(&active_queue, pthread, next);
	mcs_pdr_unlock(&queue_lock);

	/* TODO: RISCV/x86 issue! (0 is divby0, 14 is PF, etc) */
#if defined(__i386__) || defined(__x86_64__) 
	switch(trap_nr) {
		case 0:
			handle_div_by_zero(uthread, err, aux);
			break;
		case 13:
			handle_gp_fault(uthread, err, aux);
			break;
		case 14:
			handle_page_fault(uthread, err, aux);
			break;
		default:
			printf("Pthread has unhandled fault: %d\n", trap_nr);
			/* Note that uthread.c already copied out our ctx into the uth struct */
			print_user_context(&uthread->u_ctx);
			exit(-1);
	}
#else
	#error "Handling hardware faults is currently only supported on x86"
#endif
}

void pth_preempt_pending(void)
{
}

void pth_spawn_thread(uintptr_t pc_start, void *data)
{
}

/* Akaros pthread extensions / hacks */

/* Tells the pthread 2LS to not change the number of vcores.  This means it will
 * neither request vcores nor yield vcores.  Only used for testing. */
void pthread_can_vcore_request(bool can)
{
	/* checked when we would request or yield */
	can_adjust_vcores = can;
}

void pthread_need_tls(bool need)
{
	need_tls = need;
}

/* Pthread interface stuff and helpers */

int pthread_attr_init(pthread_attr_t *a)
{
 	a->stacksize = PTHREAD_STACK_SIZE;
	a->detachstate = PTHREAD_CREATE_JOINABLE;
  	return 0;
}

int pthread_attr_destroy(pthread_attr_t *a)
{
	return 0;
}

static void __pthread_free_stack(struct pthread_tcb *pt)
{
	int ret = munmap(pt->stacktop - pt->stacksize, pt->stacksize);
	assert(!ret);
}

static int __pthread_allocate_stack(struct pthread_tcb *pt)
{
	assert(pt->stacksize);
	void* stackbot = mmap(0, pt->stacksize,
	                      PROT_READ|PROT_WRITE|PROT_EXEC,
	                      MAP_POPULATE|MAP_ANONYMOUS, -1, 0);
	if (stackbot == MAP_FAILED)
		return -1; // errno set by mmap
	pt->stacktop = stackbot + pt->stacksize;
	return 0;
}

// Warning, this will reuse numbers eventually
static int get_next_pid(void)
{
	static uint32_t next_pid = 0;
	return next_pid++;
}

int pthread_attr_setstacksize(pthread_attr_t *attr, size_t stacksize)
{
	attr->stacksize = stacksize;
	return 0;
}

int pthread_attr_getstacksize(const pthread_attr_t *attr, size_t *stacksize)
{
	*stacksize = attr->stacksize;
	return 0;
}

int pthread_attr_getstack(const pthread_attr_t *__restrict __attr,
						   void **__stackaddr, size_t *__stacksize)
{
	*__stackaddr = __attr->stackaddr;
	*__stacksize = __attr->stacksize;
	return 0;
}

int pthread_getattr_np(pthread_t __th, pthread_attr_t *__attr)
{
	__attr->stackaddr = __th->stacktop - __th->stacksize;
	__attr->stacksize = __th->stacksize;
	if (__th->detached)
		__attr->detachstate = PTHREAD_CREATE_DETACHED;
	else
		__attr->detachstate = PTHREAD_CREATE_JOINABLE;
	return 0;
}

/* Do whatever init you want.  At some point call uthread_lib_init() and pass it
 * a uthread representing thread0 (int main()) */
void pthread_lib_init(void)
{
	uintptr_t mmap_block;
	struct pthread_tcb *t;
	int ret;
	/* Some testing code might call this more than once (once for a slimmed down
	 * pth 2LS, and another from pthread_create().  Also, this is racy, but the
	 * first time through we are an SCP. */
	init_once_racy(return);
	assert(!in_multi_mode());
	mcs_pdr_init(&queue_lock);
	/* Create a pthread_tcb for the main thread */
	ret = posix_memalign((void**)&t, __alignof__(struct pthread_tcb),
	                     sizeof(struct pthread_tcb));
	assert(!ret);
	memset(t, 0, sizeof(struct pthread_tcb));	/* aggressively 0 for bugs */
	t->id = get_next_pid();
	t->stacksize = USTACK_NUM_PAGES * PGSIZE;
	t->stacktop = (void*)USTACKTOP;
	t->detached = TRUE;
	t->state = PTH_RUNNING;
	t->joiner = 0;
	__sigemptyset(&t->sigmask);
	__sigemptyset(&t->sigpending);
	assert(t->id == 0);
	/* Put the new pthread (thread0) on the active queue */
	mcs_pdr_lock(&queue_lock);
	threads_active++;
	TAILQ_INSERT_TAIL(&active_queue, t, next);
	mcs_pdr_unlock(&queue_lock);
	/* Tell the kernel where and how we want to receive events.  This is just an
	 * example of what to do to have a notification turned on.  We're turning on
	 * USER_IPIs, posting events to vcore 0's vcpd, and telling the kernel to
	 * send to vcore 0.  Note sys_self_notify will ignore the vcoreid and
	 * private preference.  Also note that enable_kevent() is just an example,
	 * and you probably want to use parts of event.c to do what you want. */
	enable_kevent(EV_USER_IPI, 0, EVENT_IPI | EVENT_VCORE_PRIVATE);

	/* Handle syscall events. */
	register_ev_handler(EV_SYSCALL, pth_handle_syscall, 0);
	/* Set up the per-vcore structs to track outstanding syscalls */
	sysc_mgmt = malloc(sizeof(struct sysc_mgmt) * max_vcores());
	assert(sysc_mgmt);
#if 1   /* Independent ev_mboxes per vcore */
	/* Get a block of pages for our per-vcore (but non-VCPD) ev_qs */
	mmap_block = (uintptr_t)mmap(0, PGSIZE * 2 * max_vcores(),
	                             PROT_WRITE | PROT_READ,
	                             MAP_POPULATE | MAP_ANONYMOUS, -1, 0);
	assert(mmap_block);
	/* Could be smarter and do this on demand (in case we don't actually want
	 * max_vcores()). */
	for (int i = 0; i < max_vcores(); i++) {
		/* Each vcore needs to point to a non-VCPD ev_q */
		sysc_mgmt[i].ev_q = get_big_event_q_raw();
		sysc_mgmt[i].ev_q->ev_flags = EVENT_IPI | EVENT_INDIR | EVENT_FALLBACK;
		sysc_mgmt[i].ev_q->ev_vcore = i;
		ucq_init_raw(&sysc_mgmt[i].ev_q->ev_mbox->ev_msgs, 
		             mmap_block + (2 * i    ) * PGSIZE, 
		             mmap_block + (2 * i + 1) * PGSIZE); 
	}
	/* Technically, we should munmap and free what we've alloc'd, but the
	 * kernel will clean it up for us when we exit. */
#endif 
#if 0   /* One global ev_mbox, separate ev_q per vcore */
	struct event_mbox *sysc_mbox = malloc(sizeof(struct event_mbox));
	uintptr_t two_pages = (uintptr_t)mmap(0, PGSIZE * 2, PROT_WRITE | PROT_READ,
	                                      MAP_POPULATE | MAP_ANONYMOUS, -1, 0);
	printd("Global ucq: %08p\n", &sysc_mbox->ev_msgs);
	assert(sysc_mbox);
	assert(two_pages);
	memset(sysc_mbox, 0, sizeof(struct event_mbox));
	ucq_init_raw(&sysc_mbox->ev_msgs, two_pages, two_pages + PGSIZE);
	for (int i = 0; i < max_vcores(); i++) {
		sysc_mgmt[i].ev_q = get_event_q();
		sysc_mgmt[i].ev_q->ev_flags = EVENT_IPI | EVENT_INDIR | EVENT_FALLBACK;
		sysc_mgmt[i].ev_q->ev_vcore = i;
		sysc_mgmt[i].ev_q->ev_mbox = sysc_mbox;
	}
#endif
	/* Initialize the uthread code (we're in _M mode after this).  Doing this
	 * last so that all the event stuff is ready when we're in _M mode.  Not a
	 * big deal one way or the other.  Note that vcore_init() probably has
	 * happened, but don't rely on this.  Careful if your 2LS somehow wants to
	 * have its init stuff use things like vcore stacks or TLSs, we'll need to
	 * change this. */
	uthread_lib_init((struct uthread*)t);
	atomic_init(&threads_total, 1);			/* one for thread0 */
}

int __pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                     void *(*start_routine)(void *), void *arg)
{
	struct uth_thread_attr uth_attr = {0};
	run_once(pthread_lib_init());
	/* Create the actual thread */
	struct pthread_tcb *pthread;
	int ret = posix_memalign((void**)&pthread, __alignof__(struct pthread_tcb),
	                         sizeof(struct pthread_tcb));
	assert(!ret);
	memset(pthread, 0, sizeof(struct pthread_tcb));	/* aggressively 0 for bugs*/
	pthread->stacksize = PTHREAD_STACK_SIZE;	/* default */
	pthread->state = PTH_CREATED;
	pthread->id = get_next_pid();
	pthread->detached = FALSE;				/* default */
	pthread->joiner = 0;
	pthread->sigmask = ((pthread_t)current_uthread)->sigmask;
	__sigemptyset(&pthread->sigpending);
	pthread->sigdata = NULL;
	/* Respect the attributes */
	if (attr) {
		if (attr->stacksize)					/* don't set a 0 stacksize */
			pthread->stacksize = attr->stacksize;
		if (attr->detachstate == PTHREAD_CREATE_DETACHED)
			pthread->detached = TRUE;
	}
	/* allocate a stack */
	if (__pthread_allocate_stack(pthread))
		printf("We're fucked\n");
	/* Set the u_tf to start up in __pthread_run, which will call the real
	 * start_routine and pass it the arg.  Note those aren't set until later in
	 * pthread_create(). */
	init_user_ctx(&pthread->uthread.u_ctx, (uintptr_t)&__pthread_run,
	              (uintptr_t)(pthread->stacktop));
	pthread->start_routine = start_routine;
	pthread->arg = arg;
	/* Initialize the uthread */
	if (need_tls)
		uth_attr.want_tls = TRUE;
	uthread_init((struct uthread*)pthread, &uth_attr);
	*thread = pthread;
	atomic_inc(&threads_total);
	return 0;
}

int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start_routine)(void *), void *arg)
{
	if (!__pthread_create(thread, attr, start_routine, arg))
		pth_thread_runnable((struct uthread*)*thread);
	return 0;
}

/* Helper that all pthread-controlled yield paths call.  Just does some
 * accounting.  This is another example of how the much-loathed (and loved)
 * active queue is keeping us honest.  Need to export for sem and friends. */
void __pthread_generic_yield(struct pthread_tcb *pthread)
{
	mcs_pdr_lock(&queue_lock);
	threads_active--;
	TAILQ_REMOVE(&active_queue, pthread, next);
	mcs_pdr_unlock(&queue_lock);
}

/* Callback/bottom half of join, called from __uthread_yield (vcore context).
 * join_target is who we are trying to join on (and who is calling exit). */
static void __pth_join_cb(struct uthread *uthread, void *arg)
{
	struct pthread_tcb *pthread = (struct pthread_tcb*)uthread;
	struct pthread_tcb *join_target = (struct pthread_tcb*)arg;
	struct pthread_tcb *temp_pth = 0;
	__pthread_generic_yield(pthread);
	/* We're trying to join, yield til we get woken up */
	pthread->state = PTH_BLK_JOINING;	/* could do this front-side */
	/* Put ourselves in the join target's joiner slot.  If we get anything back,
	 * we lost the race and need to wake ourselves.  Syncs with __pth_exit_cb.*/
	temp_pth = atomic_swap_ptr((void**)&join_target->joiner, pthread);
	/* After that atomic swap, the pthread might be woken up (if it succeeded),
	 * so don't touch pthread again after that (this following if () is okay).*/
	if (temp_pth) {		/* temp_pth != 0 means they exited first */
		assert(temp_pth == join_target);	/* Sanity */
		/* wake ourselves, not the exited one! */
		printd("[pth] %08p already exit, rewaking ourselves, joiner %08p\n",
		       temp_pth, pthread);
		pth_thread_runnable(uthread);	/* wake ourselves */
	}
}

int pthread_join(struct pthread_tcb *join_target, void **retval)
{
	/* Not sure if this is the right semantics.  There is a race if we deref
	 * join_target and he is already freed (which would have happened if he was
	 * detached. */
	if (join_target->detached) {
		printf("[pthread] trying to join on a detached pthread");
		return -1;
	}
	/* See if it is already done, to avoid the pain of a uthread_yield() (the
	 * early check is an optimization, pth_thread_yield() handles the race). */
	if (!join_target->joiner) {
		uthread_yield(TRUE, __pth_join_cb, join_target);
		/* When we return/restart, the thread will be done */
	} else {
		assert(join_target->joiner == join_target);	/* sanity check */
	}
	if (retval)
		*retval = join_target->retval;
	free(join_target);
	return 0;
}

/* Callback/bottom half of exit.  Syncs with __pth_join_cb.  Here's how it
 * works: the slot for joiner is initially 0.  Joiners try to swap themselves
 * into that spot.  Exiters try to put 'themselves' into it.  Whoever gets 0
 * back won the race.  If the exiter lost the race, it must wake up the joiner
 * (which was the value from temp_pth).  If the joiner lost the race, it must
 * wake itself up, and for sanity reasons can ensure the value from temp_pth is
 * the join target). */
static void __pth_exit_cb(struct uthread *uthread, void *junk)
{
	struct pthread_tcb *pthread = (struct pthread_tcb*)uthread;
	struct pthread_tcb *temp_pth = 0;
	__pthread_generic_yield(pthread);
	/* Catch some bugs */
	pthread->state = PTH_EXITING;
	/* Destroy the pthread */
	uthread_cleanup(uthread);
	/* Cleanup, mirroring pthread_create() */
	__pthread_free_stack(pthread);
	/* TODO: race on detach state (see join) */
	if (pthread->detached) {
		free(pthread);
	} else {
		/* See if someone is joining on us.  If not, we're done (and the
		 * joiner will wake itself when it saw us there instead of 0). */
		temp_pth = atomic_swap_ptr((void**)&pthread->joiner, pthread);
		if (temp_pth) {
			/* they joined before we exited, we need to wake them */
			printd("[pth] %08p exiting, waking joiner %08p\n",
			       pthread, temp_pth);
			pth_thread_runnable((struct uthread*)temp_pth);
		}
	}
	/* If we were the last pthread, we exit for the whole process.  Keep in mind
	 * that thread0 is counted in this, so this will only happen if that thread
	 * calls pthread_exit(). */
	if ((atomic_fetch_and_add(&threads_total, -1) == 1))
		exit(0);
}

void pthread_exit(void *ret)
{
	struct pthread_tcb *pthread = pthread_self();
	/* Some apps could call pthread_exit before initing.  This will slow down
	 * our pthread exits slightly. */
	pthread_lib_init();
	pthread->retval = ret;
	destroy_dtls();
	uthread_yield(FALSE, __pth_exit_cb, 0);
}

/* Callback/bottom half of yield.  For those writing these pth callbacks, the
 * minimum is call generic, set state (communicate with runnable), then do
 * something that causes it to be runnable in the future (or right now). */
static void __pth_yield_cb(struct uthread *uthread, void *junk)
{
	struct pthread_tcb *pthread = (struct pthread_tcb*)uthread;
	__pthread_generic_yield(pthread);
	pthread->state = PTH_BLK_YIELDING;
	/* just immediately restart it */
	pth_thread_runnable(uthread);
}

/* Cooperative yielding of the processor, to allow other threads to run */
int pthread_yield(void)
{
	uthread_yield(TRUE, __pth_yield_cb, 0);
	return 0;
}

int pthread_mutexattr_init(pthread_mutexattr_t* attr)
{
  attr->type = PTHREAD_MUTEX_DEFAULT;
  return 0;
}

int pthread_mutexattr_destroy(pthread_mutexattr_t* attr)
{
  return 0;
}

int pthread_attr_setdetachstate(pthread_attr_t *__attr, int __detachstate)
{
	__attr->detachstate = __detachstate;
	return 0;
}

int pthread_mutexattr_gettype(const pthread_mutexattr_t* attr, int* type)
{
  *type = attr ? attr->type : PTHREAD_MUTEX_DEFAULT;
  return 0;
}

int pthread_mutexattr_settype(pthread_mutexattr_t* attr, int type)
{
  if(type != PTHREAD_MUTEX_NORMAL)
    return EINVAL;
  attr->type = type;
  return 0;
}

int pthread_mutex_init(pthread_mutex_t* m, const pthread_mutexattr_t* attr)
{
  m->attr = attr;
  atomic_init(&m->lock, 0);
  return 0;
}

/* Helper for spinning sync, returns TRUE if it is okay to keep spinning.
 *
 * Alternatives include:
 * 		old_count <= num_vcores() (barrier code, pass in old_count as *state, 
 * 		                           but this only works if every awake pthread
 * 		                           will belong to the barrier).
 * 		just spin for a bit       (use *state to track spins)
 * 		FALSE                     (always is safe)
 * 		etc...
 * 'threads_ready' isn't too great since sometimes it'll be non-zero when it is
 * about to become 0.  We really want "I have no threads waiting to run that
 * aren't going to run on their on unless this core yields instead of spins". */
/* TODO: consider making this a 2LS op */
static inline bool safe_to_spin(unsigned int *state)
{
	return !threads_ready;
}

/* Set *spun to 0 when calling this the first time.  It will yield after 'spins'
 * calls.  Use this for adaptive mutexes and such. */
static inline void spin_to_sleep(unsigned int spins, unsigned int *spun)
{
	if ((*spun)++ == spins) {
		pthread_yield();
		*spun = 0;
	}
}

int pthread_mutex_lock(pthread_mutex_t* m)
{
	unsigned int spinner = 0;
	while(pthread_mutex_trylock(m))
		while(*(volatile size_t*)&m->lock) {
			cpu_relax();
			spin_to_sleep(PTHREAD_MUTEX_SPINS, &spinner);
		}
	/* normally we'd need a wmb() and a wrmb() after locking, but the
	 * atomic_swap handles the CPU mb(), so just a cmb() is necessary. */
	cmb();
	return 0;
}

int pthread_mutex_trylock(pthread_mutex_t* m)
{
  return atomic_swap(&m->lock, 1) == 0 ? 0 : EBUSY;
}

int pthread_mutex_unlock(pthread_mutex_t* m)
{
  /* keep reads and writes inside the protected region */
  rwmb();
  wmb();
  atomic_set(&m->lock, 0);
  return 0;
}

int pthread_mutex_destroy(pthread_mutex_t* m)
{
  return 0;
}

int pthread_cond_init(pthread_cond_t *c, const pthread_condattr_t *a)
{
	TAILQ_INIT(&c->waiters);
	spin_pdr_init(&c->spdr_lock);
	if (a) {
		c->attr_pshared = a->pshared;
		c->attr_clock = a->clock;
	} else {
		c->attr_pshared = PTHREAD_PROCESS_PRIVATE;
		c->attr_clock = 0;
	}
	return 0;
}

int pthread_cond_destroy(pthread_cond_t *c)
{
	return 0;
}

int pthread_cond_broadcast(pthread_cond_t *c)
{
	unsigned int nr_woken = 0;	/* assuming less than 4 bil threads */
	struct pthread_queue restartees = TAILQ_HEAD_INITIALIZER(restartees);
	struct pthread_tcb *pthread_i;
	spin_pdr_lock(&c->spdr_lock);
	/* moves all items from waiters onto the end of restartees */
	TAILQ_CONCAT(&restartees, &c->waiters, next);
	spin_pdr_unlock(&c->spdr_lock);
	/* Do the work of pth_thread_runnable().  We're in uth context here, but I
	 * think it's okay.  When we need to (when locking) we drop into VC ctx, as
	 * far as the kernel and other cores are concerned. */
	TAILQ_FOREACH(pthread_i, &restartees, next) {
		pthread_i->state = PTH_RUNNABLE;
		nr_woken++;
	}
	/* Amortize the lock grabbing over all restartees */
	mcs_pdr_lock(&queue_lock);
	threads_ready += nr_woken;
	TAILQ_CONCAT(&ready_queue, &restartees, next);
	mcs_pdr_unlock(&queue_lock);
	if (can_adjust_vcores)
		vcore_request(threads_ready);
	return 0;
}

/* spec says this needs to work regardless of whether or not it holds the mutex
 * already. */
int pthread_cond_signal(pthread_cond_t *c)
{
	struct pthread_tcb *pthread;
	spin_pdr_lock(&c->spdr_lock);
	pthread = TAILQ_FIRST(&c->waiters);
	if (!pthread) {
		spin_pdr_unlock(&c->spdr_lock);
		return 0;
	}
	TAILQ_REMOVE(&c->waiters, pthread, next);
	spin_pdr_unlock(&c->spdr_lock);
	pth_thread_runnable((struct uthread*)pthread);
	return 0;
}

/* Communicate btw cond_wait and its callback */
struct cond_junk {
	pthread_cond_t				*c;
	pthread_mutex_t				*m;
};

/* Callback/bottom half of cond wait.  For those writing these pth callbacks,
 * the minimum is call generic, set state (communicate with runnable), then do
 * something that causes it to be runnable in the future (or right now). */
static void __pth_wait_cb(struct uthread *uthread, void *junk)
{
	struct pthread_tcb *pthread = (struct pthread_tcb*)uthread;
	pthread_cond_t *c = ((struct cond_junk*)junk)->c;
	pthread_mutex_t *m = ((struct cond_junk*)junk)->m;
	/* this removes us from the active list; we can reuse next below */
	__pthread_generic_yield(pthread);
	pthread->state = PTH_BLK_MUTEX;
	spin_pdr_lock(&c->spdr_lock);
	TAILQ_INSERT_TAIL(&c->waiters, pthread, next);
	spin_pdr_unlock(&c->spdr_lock);
	pthread_mutex_unlock(m);
}

int pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m)
{
	struct cond_junk local_junk;
	local_junk.c = c;
	local_junk.m = m;
	uthread_yield(TRUE, __pth_wait_cb, &local_junk);
	pthread_mutex_lock(m);
	return 0;
}

int pthread_condattr_init(pthread_condattr_t *a)
{
	a->pshared = PTHREAD_PROCESS_PRIVATE;
	a->clock = 0;
	return 0;
}

int pthread_condattr_destroy(pthread_condattr_t *a)
{
	return 0;
}

int pthread_condattr_getpshared(pthread_condattr_t *a, int *s)
{
	*s = a->pshared;
	return 0;
}

int pthread_condattr_setpshared(pthread_condattr_t *a, int s)
{
	a->pshared = s;
	if (s == PTHREAD_PROCESS_SHARED) {
		printf("Warning: we don't do shared pthread condvars btw diff MCPs\n");
		return -1;
	}
	return 0;
}

int pthread_condattr_getclock(const pthread_condattr_t *attr,
                              clockid_t *clock_id)
{
	*clock_id = attr->clock;
}

int pthread_condattr_setclock(pthread_condattr_t *attr, clockid_t clock_id)
{
	printf("Warning: we don't do pthread condvar clock stuff\n");
	attr->clock = clock_id;
}

pthread_t pthread_self()
{
  return (struct pthread_tcb*)current_uthread;
}

int pthread_equal(pthread_t t1, pthread_t t2)
{
  return t1 == t2;
}

int pthread_once(pthread_once_t* once_control, void (*init_routine)(void))
{
  if (atomic_swap_u32(once_control, 1) == 0)
    init_routine();
  return 0;
}

int pthread_barrier_init(pthread_barrier_t *b,
                         const pthread_barrierattr_t *a, int count)
{
	b->total_threads = count;
	b->sense = 0;
	atomic_set(&b->count, count);
	spin_pdr_init(&b->lock);
	TAILQ_INIT(&b->waiters);
	b->nr_waiters = 0;
	return 0;
}

struct barrier_junk {
	pthread_barrier_t				*b;
	int								ls;
};

/* Callback/bottom half of barrier. */
static void __pth_barrier_cb(struct uthread *uthread, void *junk)
{
	struct pthread_tcb *pthread = (struct pthread_tcb*)uthread;
	pthread_barrier_t *b = ((struct barrier_junk*)junk)->b;
	int ls = ((struct barrier_junk*)junk)->ls;
	/* Removes from active list, we can reuse.  must also restart */
	__pthread_generic_yield(pthread);
	/* TODO: if we used a trylock, we could bail as soon as we see sense */
	spin_pdr_lock(&b->lock);
	/* If sense is ls (our free value), we lost the race and shouldn't sleep */
	if (b->sense == ls) {
		/* TODO: i'd like to fast-path the wakeup, skipping pth_runnable */
		pthread->state = PTH_BLK_YIELDING;	/* not sure which state for this */
		spin_pdr_unlock(&b->lock);
		pth_thread_runnable(uthread);
		return;
	}
	/* otherwise, we sleep */
	pthread->state = PTH_BLK_MUTEX;	/* TODO: consider ignoring this */
	TAILQ_INSERT_TAIL(&b->waiters, pthread, next);
	b->nr_waiters++;
	spin_pdr_unlock(&b->lock);
}

/* We assume that the same threads participating in the barrier this time will
 * also participate next time.  Imagine a thread stopped right after its fetch
 * and add - we know it is coming through eventually.  We finish and change the
 * sense, which should allow the delayed thread to eventually break through.
 * But if another n threads come in first, we'll set the sense back to the old
 * value, thereby catching the delayed thread til the next barrier. 
 *
 * A note on preemption: if any thread gets preempted and it is never dealt
 * with, eventually we deadlock, with all threads waiting on the last one to
 * enter (and any stragglers from one run will be the last in the next run).
 * One way or another, we need to handle preemptions.  The current 2LS requests
 * an IPI for a preempt, so we'll be fine.  Any other strategies will need to
 * consider how barriers work.  Any time we sleep, we'll be okay (since that
 * frees up our core to handle preemptions/run other threads. */
int pthread_barrier_wait(pthread_barrier_t *b)
{
	unsigned int spin_state = 0;
	int ls = !b->sense;	/* when b->sense is the value we read, then we're free*/
	int nr_waiters;
	struct pthread_queue restartees = TAILQ_HEAD_INITIALIZER(restartees);
	struct pthread_tcb *pthread_i;
	struct barrier_junk local_junk;
	
	long old_count = atomic_fetch_and_add(&b->count, -1);

	if (old_count == 1) {
		printd("Thread %d is last to hit the barrier, resetting...\n",
		       pthread_self()->id);
		/* TODO: we might want to grab the lock right away, so a few short
		 * circuit faster? */
		atomic_set(&b->count, b->total_threads);
		/* we still need to maintain ordering btw count and sense, in case
		 * another thread doesn't sleep (if we wrote sense first, they could
		 * break out, race around, and muck with count before it is time) */
		/* wmb(); handled by the spin lock */
		spin_pdr_lock(&b->lock);
		/* Sense is only protected in addition to decisions to sleep */
		b->sense = ls;	/* set to free everyone */
		/* All access to nr_waiters is protected by the lock */
		if (!b->nr_waiters) {
			spin_pdr_unlock(&b->lock);
			return PTHREAD_BARRIER_SERIAL_THREAD;
		}
		TAILQ_CONCAT(&restartees, &b->waiters, next);
		nr_waiters = b->nr_waiters;
		b->nr_waiters = 0;
		spin_pdr_unlock(&b->lock);
		/* TODO: do we really need this state tracking? */
		TAILQ_FOREACH(pthread_i, &restartees, next)
			pthread_i->state = PTH_RUNNABLE;
		/* bulk restart waiters (skipping pth_thread_runnable()) */
		mcs_pdr_lock(&queue_lock);
		threads_ready += nr_waiters;
		TAILQ_CONCAT(&ready_queue, &restartees, next);
		mcs_pdr_unlock(&queue_lock);
		if (can_adjust_vcores)
			vcore_request(threads_ready);
		return PTHREAD_BARRIER_SERIAL_THREAD;
	} else {
		/* Spin if there are no other threads to run.  No sense sleeping */
		do {
			if (b->sense == ls)
				return 0;
			cpu_relax();
		} while (safe_to_spin(&spin_state));

		/* Try to sleep, when we wake/return, we're free to go */
		local_junk.b = b;
		local_junk.ls = ls;
		uthread_yield(TRUE, __pth_barrier_cb, &local_junk);
		// assert(b->sense == ls);
		return 0;
	}
}

int pthread_barrier_destroy(pthread_barrier_t *b)
{
	assert(TAILQ_EMPTY(&b->waiters));
	assert(!b->nr_waiters);
	/* Free any locks (if we end up using an MCS) */
	return 0;
}

int pthread_detach(pthread_t thread)
{
	/* TODO: race on this state.  Someone could be trying to join now */
	thread->detached = TRUE;
	return 0;
}

int pthread_kill(pthread_t thread, int signo)
{
	// Slightly racy with clearing of mask when triggering the signal, but
	// that's OK, as signals are inherently racy since they don't queue up.
	return sigaddset(&thread->sigpending, signo);
}


int pthread_sigmask(int how, const sigset_t *set, sigset_t *oset)
{
	if (how != SIG_BLOCK && how != SIG_SETMASK && how != SIG_UNBLOCK) {
		errno = EINVAL;
		return -1;
	}

	pthread_t pthread = ((struct pthread_tcb*)current_uthread);
	if (oset)
		*oset = pthread->sigmask;
	switch (how) {
		case SIG_BLOCK:
			pthread->sigmask = pthread->sigmask | *set;
			break;
		case SIG_SETMASK:
			pthread->sigmask = *set;
			break;
		case SIG_UNBLOCK:
			pthread->sigmask = pthread->sigmask & ~(*set);
			break;
	}
	// Ensures any signals we just unmasked get processed if they are pending
	pthread_yield();
	return 0;
}

int pthread_sigqueue(pthread_t *thread, int sig, const union sigval value)
{
	printf("pthread_sigqueue is not yet implemented!");
	return -1;
}

int pthread_key_create(pthread_key_t *key, void (*destructor)(void*))
{
	*key = dtls_key_create(destructor);
	assert(key);
	return 0;
}

int pthread_key_delete(pthread_key_t key)
{
	dtls_key_delete(key);
	return 0;
}

void *pthread_getspecific(pthread_key_t key)
{
	return get_dtls(key);
}

int pthread_setspecific(pthread_key_t key, const void *value)
{
	set_dtls(key, (void*)value);
	return 0;
}

int pthread_mutex_timedlock (pthread_mutex_t *__restrict __mutex,
					const struct timespec *__restrict
					__abstime)
{
	fprintf(stderr, "Unsupported %s!", __FUNCTION__);
	abort();
	return -1;
}
int pthread_cond_timedwait (pthread_cond_t *__restrict __cond,
				   pthread_mutex_t *__restrict __mutex,
				   const struct timespec *__restrict __abstime)
{
	fprintf(stderr, "Unsupported %s!", __FUNCTION__);
	abort();
	return -1;
}

int pthread_cancel (pthread_t __th)
{
	fprintf(stderr, "Unsupported %s!", __FUNCTION__);
	abort();
	return -1;
}
