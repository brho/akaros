#include <ros/trapframe.h>
#include "pthread.h"
#include <parlib/vcore.h>
#include <parlib/mcs.h>
#include <stdlib.h>
#include <string.h>
#include <parlib/assert.h>
#include <stdio.h>
#include <errno.h>
#include <parlib/parlib.h>
#include <ros/event.h>
#include <parlib/arch/atomic.h>
#include <parlib/arch/arch.h>
#include <sys/queue.h>
#include <sys/mman.h>
#include <parlib/event.h>
#include <parlib/ucq.h>
#include <parlib/signal.h>
#include <parlib/arch/trap.h>
#include <parlib/ros_debug.h>
#include <parlib/stdio.h>

struct pthread_queue ready_queue = TAILQ_HEAD_INITIALIZER(ready_queue);
struct pthread_queue active_queue = TAILQ_HEAD_INITIALIZER(active_queue);
struct mcs_pdr_lock queue_lock;
int threads_ready = 0;
int threads_active = 0;
atomic_t threads_total;
bool need_tls = TRUE;

/* Array of per-vcore structs to manage waiting on syscalls and handling
 * overflow.  Init'd in pth_init(). */
struct sysc_mgmt *sysc_mgmt = 0;

/* Helper / local functions */
static int get_next_pid(void);
static inline void pthread_exit_no_cleanup(void *ret);

/* Pthread 2LS operations */
static void pth_sched_init(void);
static void pth_sched_entry(void);
static void pth_thread_runnable(struct uthread *uthread);
static void pth_thread_paused(struct uthread *uthread);
static void pth_thread_blockon_sysc(struct uthread *uthread, void *sysc);
static void pth_thread_has_blocked(struct uthread *uthread, int flags);
static void pth_thread_refl_fault(struct uthread *uth,
                                  struct user_context *ctx);
static void pth_thread_exited(struct uthread *uth);
static struct uthread *pth_thread_create(void *(*func)(void *), void *arg);
static void pth_thread_bulk_runnable(uth_sync_t *wakees);

/* Event Handlers */
static void pth_handle_syscall(struct event_msg *ev_msg, unsigned int ev_type,
                               void *data);

struct schedule_ops pthread_sched_ops = {
	.sched_init = pth_sched_init,
	.sched_entry = pth_sched_entry,
	.thread_runnable = pth_thread_runnable,
	.thread_paused = pth_thread_paused,
	.thread_blockon_sysc = pth_thread_blockon_sysc,
	.thread_has_blocked = pth_thread_has_blocked,
	.thread_refl_fault = pth_thread_refl_fault,
	.thread_exited = pth_thread_exited,
	.thread_create = pth_thread_create,
	.thread_bulk_runnable = pth_thread_bulk_runnable,
};

struct schedule_ops *sched_ops = &pthread_sched_ops;

/* Static helpers */
static void __pthread_free_stack(struct pthread_tcb *pt);
static int __pthread_allocate_stack(struct pthread_tcb *pt);
static void __pth_yield_cb(struct uthread *uthread, void *junk);

/* Called from vcore entry.  Options usually include restarting whoever was
 * running there before or running a new thread.  Events are handled out of
 * event.c (table of function pointers, stuff like that). */
static void __attribute__((noreturn)) pth_sched_entry(void)
{
	uint32_t vcoreid = vcore_id();
	if (current_uthread) {
		/* Prep the pthread to run any pending posix signal handlers registered
         * via pthread_kill once it is restored. */
		uthread_prep_pending_signals(current_uthread);
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
			TAILQ_REMOVE(&ready_queue, new_thread, tq_next);
			assert(new_thread->state == PTH_RUNNABLE);
			new_thread->state = PTH_RUNNING;
			TAILQ_INSERT_TAIL(&active_queue, new_thread, tq_next);
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
		 * bit before yielding. */
		vcore_yield(FALSE);
	} while (1);
	/* Prep the pthread to run any pending posix signal handlers registered
     * via pthread_kill once it is restored. */
	uthread_prep_pending_signals((struct uthread*)new_thread);
	/* Run the thread itself */
	run_uthread((struct uthread*)new_thread);
	assert(0);
}

/* Could move this, along with start_routine and arg, into the 2LSs */
static void __pthread_run(void)
{
	struct pthread_tcb *me = pthread_self();
	pthread_exit_no_cleanup(me->start_routine(me->arg));
}

/* GIANT WARNING: if you make any changes to this, also change the broadcast
 * wakeups (cond var, barrier, etc) */
static void pth_thread_runnable(struct uthread *uthread)
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
		case (PTH_BLK_SYSC):
		case (PTH_BLK_PAUSED):
		case (PTH_BLK_MUTEX):
		case (PTH_BLK_MISC):
			/* can do whatever for each of these cases */
			break;
		default:
			panic("Odd state %d for pthread %08p\n", pthread->state, pthread);
	}
	pthread->state = PTH_RUNNABLE;
	/* Insert the newly created thread into the ready queue of threads.
	 * It will be removed from this queue later when vcore_entry() comes up */
	mcs_pdr_lock(&queue_lock);
	/* Again, GIANT WARNING: if you change this, change batch wakeup code */
	TAILQ_INSERT_TAIL(&ready_queue, pthread, tq_next);
	threads_ready++;
	mcs_pdr_unlock(&queue_lock);
	/* Smarter schedulers should look at the num_vcores() and how much work is
	 * going on to make a decision about how many vcores to request. */
	vcore_request_more(threads_ready);
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
static void pth_thread_paused(struct uthread *uthread)
{
	struct pthread_tcb *pthread = (struct pthread_tcb*)uthread;

	__pthread_generic_yield(pthread);
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
static void pth_thread_blockon_sysc(struct uthread *uthread, void *syscall)
{
	struct syscall *sysc = (struct syscall*)syscall;
	int old_flags;
	uint32_t vcoreid = vcore_id();
	struct pthread_tcb *pthread = (struct pthread_tcb*)uthread;

	__pthread_generic_yield(pthread);
	pthread->state = PTH_BLK_SYSC;
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

static void pth_thread_has_blocked(struct uthread *uthread, int flags)
{
	struct pthread_tcb *pthread = (struct pthread_tcb*)uthread;

	__pthread_generic_yield(pthread);
	/* Whatever we do here, we are mostly communicating to our future selves in
	 * pth_thread_runnable(), which gets called by whoever triggered this
	 * callback */
	switch (flags) {
	case UTH_EXT_BLK_YIELD:
		pthread->state = PTH_BLK_YIELDING;
		break;
	case UTH_EXT_BLK_MUTEX:
		pthread->state = PTH_BLK_MUTEX;
		break;
	default:
		pthread->state = PTH_BLK_MISC;
	};
}

static void __signal_and_restart(struct uthread *uthread,
                                 int signo, int code, void *addr)
{
	uthread_prep_signal_from_fault(uthread, signo, code, addr);
	pth_thread_runnable(uthread);
}

static void handle_div_by_zero(struct uthread *uthread, unsigned int err,
                               unsigned long aux)
{
	__signal_and_restart(uthread, SIGFPE, FPE_INTDIV, (void*)aux);
}

static void handle_gp_fault(struct uthread *uthread, unsigned int err,
                            unsigned long aux)
{
	__signal_and_restart(uthread, SIGSEGV, SEGV_ACCERR, (void*)aux);
}

static void handle_page_fault(struct uthread *uthread, unsigned int err,
                              unsigned long aux)
{
	struct pthread_tcb *pthread = (struct pthread_tcb*)uthread;
	if (!(err & PF_VMR_BACKED)) {
		__signal_and_restart(uthread, SIGSEGV, SEGV_MAPERR, (void*)aux);
	} else {
		syscall_async(&uthread->local_sysc, SYS_populate_va, aux, 1);
		__block_uthread_on_async_sysc(uthread);
	}
}

static void pth_thread_refl_hw_fault(struct uthread *uthread,
                                     unsigned int trap_nr,
                                     unsigned int err, unsigned long aux)
{
	struct pthread_tcb *pthread = (struct pthread_tcb*)uthread;

	__pthread_generic_yield(pthread);
	pthread->state = PTH_BLK_SYSC;

	switch (trap_nr) {
	case HW_TRAP_DIV_ZERO:
		handle_div_by_zero(uthread, err, aux);
		break;
	case HW_TRAP_GP_FAULT:
		handle_gp_fault(uthread, err, aux);
		break;
	case HW_TRAP_PAGE_FAULT:
		handle_page_fault(uthread, err, aux);
		break;
	default:
		printf("Pthread has unhandled fault: %d, err: %d, aux: %p\n",
		       trap_nr, err, aux);
		/* Note that uthread.c already copied out our ctx into the uth
		 * struct */
		print_user_context(&uthread->u_ctx);
		printf("Turn on printx to spew unhandled, malignant trap info\n");
		exit(-1);
	}
}

static void pth_thread_refl_fault(struct uthread *uth,
                                  struct user_context *ctx)
{
	switch (ctx->type) {
	case ROS_HW_CTX:
		pth_thread_refl_hw_fault(uth, __arch_refl_get_nr(ctx),
		                         __arch_refl_get_err(ctx),
		                         __arch_refl_get_aux(ctx));
		break;
	default:
		assert(0);
	}
}

static void pth_thread_exited(struct uthread *uth)
{
	struct pthread_tcb *pthread = (struct pthread_tcb*)uth;

	__pthread_generic_yield(pthread);
	/* Catch some bugs */
	pthread->state = PTH_EXITING;
	/* Destroy the pthread */
	uthread_cleanup(uth);
	/* Cleanup, mirroring pthread_create() */
	__pthread_free_stack(pthread);
	/* If we were the last pthread, we exit for the whole process.  Keep in mind
	 * that thread0 is counted in this, so this will only happen if that thread
	 * calls pthread_exit(). */
	if ((atomic_fetch_and_add(&threads_total, -1) == 1))
		exit(0);
}

/* Careful, if someone used the pthread_need_tls() hack to turn off TLS, it will
 * also be turned off for these threads. */
static struct uthread *pth_thread_create(void *(*func)(void *), void *arg)
{
	struct pthread_tcb *pth;
	int ret;

	ret = pthread_create(&pth, NULL, func, arg);
	return ret == 0 ? (struct uthread*)pth : NULL;
}

static void pth_thread_bulk_runnable(uth_sync_t *wakees)
{
	struct uthread *uth_i;
	struct pthread_tcb *pth_i;

	/* Amortize the lock grabbing over all restartees */
	mcs_pdr_lock(&queue_lock);
	while ((uth_i = __uth_sync_get_next(wakees))) {
		pth_i = (struct pthread_tcb*)uth_i;
		pth_i->state = PTH_RUNNABLE;
		TAILQ_INSERT_TAIL(&ready_queue, pth_i, tq_next);
		threads_ready++;
	}
	mcs_pdr_unlock(&queue_lock);
	vcore_request_more(threads_ready);
}

/* Akaros pthread extensions / hacks */

/* Careful using this - glibc and gcc are likely to use TLS without you knowing
 * it. */
void pthread_need_tls(bool need)
{
	need_tls = need;
}

/* Pthread interface stuff and helpers */

int pthread_attr_init(pthread_attr_t *a)
{
	a->stackaddr = 0;
 	a->stacksize = PTHREAD_STACK_SIZE;
	a->detachstate = PTHREAD_CREATE_JOINABLE;
	/* priority and policy should be set by anyone changing inherit. */
	a->sched_priority = 0;
	a->sched_policy = 0;
	a->sched_inherit = PTHREAD_INHERIT_SCHED;
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
	int force_a_page_fault;
	assert(pt->stacksize);
	void* stackbot = mmap(0, pt->stacksize,
	                      PROT_READ|PROT_WRITE|PROT_EXEC,
	                      MAP_ANONYMOUS, -1, 0);
	if (stackbot == MAP_FAILED)
		return -1; // errno set by mmap
	pt->stacktop = stackbot + pt->stacksize;
	/* Want the top of the stack populated, but not the rest of the stack;
	 * that'll grow on demand (up to pt->stacksize) */
	force_a_page_fault = ACCESS_ONCE(*(int*)(pt->stacktop - sizeof(int)));
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

int pthread_attr_setguardsize(pthread_attr_t *attr, size_t guardsize)
{
	attr->guardsize = guardsize;
	return 0;
}

int pthread_attr_getguardsize(pthread_attr_t *attr, size_t *guardsize)
{
	*guardsize = attr->guardsize;
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
	struct uthread *uth = (struct uthread*)__th;

	__attr->stackaddr = __th->stacktop - __th->stacksize;
	__attr->stacksize = __th->stacksize;
	if (atomic_read(&uth->join_ctl.state) == UTH_JOIN_DETACHED)
		__attr->detachstate = PTHREAD_CREATE_DETACHED;
	else
		__attr->detachstate = PTHREAD_CREATE_JOINABLE;
	return 0;
}

/* Do whatever init you want.  At some point call uthread_2ls_init() and pass it
 * a uthread representing thread0 (int main()) */
void pth_sched_init(void)
{
	uintptr_t mmap_block;
	struct pthread_tcb *t;
	int ret;

	mcs_pdr_init(&queue_lock);
	/* Create a pthread_tcb for the main thread */
	ret = posix_memalign((void**)&t, __alignof__(struct pthread_tcb),
	                     sizeof(struct pthread_tcb));
	assert(!ret);
	memset(t, 0, sizeof(struct pthread_tcb));	/* aggressively 0 for bugs */
	t->id = get_next_pid();
	t->stacksize = USTACK_NUM_PAGES * PGSIZE;
	t->stacktop = (void*)USTACKTOP;
	t->state = PTH_RUNNING;
	/* implies that sigmasks are longs, which they are. */
	assert(t->id == 0);
	t->sched_policy = SCHED_FIFO;
	t->sched_priority = 0;
	SLIST_INIT(&t->cr_stack);
	/* Put the new pthread (thread0) on the active queue */
	mcs_pdr_lock(&queue_lock);
	threads_active++;
	TAILQ_INSERT_TAIL(&active_queue, t, tq_next);
	mcs_pdr_unlock(&queue_lock);
	/* Tell the kernel where and how we want to receive events.  This is just an
	 * example of what to do to have a notification turned on.  We're turning on
	 * USER_IPIs, posting events to vcore 0's vcpd, and telling the kernel to
	 * send to vcore 0.  Note sys_self_notify will ignore the vcoreid and
	 * private preference.  Also note that enable_kevent() is just an example,
	 * and you probably want to use parts of event.c to do what you want. */
	enable_kevent(EV_USER_IPI, 0, EVENT_IPI | EVENT_VCORE_PRIVATE);
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
		sysc_mgmt[i].ev_q = get_eventq_raw();
		sysc_mgmt[i].ev_q->ev_flags = EVENT_IPI | EVENT_INDIR |
		                              EVENT_SPAM_INDIR | EVENT_WAKEUP;
		sysc_mgmt[i].ev_q->ev_vcore = i;
		sysc_mgmt[i].ev_q->ev_mbox->type = EV_MBOX_UCQ;
		ucq_init_raw(&sysc_mgmt[i].ev_q->ev_mbox->ucq,
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
	sysc_mbox->type = EV_MBOX_UCQ;
	ucq_init_raw(&sysc_mbox->ucq, two_pages, two_pages + PGSIZE);
	for (int i = 0; i < max_vcores(); i++) {
		sysc_mgmt[i].ev_q = get_eventq_slim();
		sysc_mgmt[i].ev_q->ev_flags = EVENT_IPI | EVENT_INDIR |
		                              EVENT_SPAM_INDIR | EVENT_WAKEUP;
		sysc_mgmt[i].ev_q->ev_vcore = i;
		sysc_mgmt[i].ev_q->ev_mbox = sysc_mbox;
	}
#endif
	uthread_2ls_init((struct uthread*)t, pth_handle_syscall, NULL);
	atomic_init(&threads_total, 1);			/* one for thread0 */
}

/* Make sure our scheduler runs inside an MCP rather than an SCP. */
void pthread_mcp_init()
{
	/* Prevent this from happening more than once. */
	parlib_init_once_racy(return);

	uthread_mcp_init();
	/* From here forward we are an MCP running on vcore 0. Could consider doing
	 * other pthread specific initialization based on knowing we are an mcp
	 * after this point. */
}

int __pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                     void *(*start_routine)(void *), void *arg)
{
	struct uth_thread_attr uth_attr = {0};
	struct pthread_tcb *parent;
	struct pthread_tcb *pthread;
	int ret;

	/* For now, unconditionally become an mcp when creating a pthread (if not
	 * one already). This may change in the future once we support 2LSs in an
	 * SCP. */
	pthread_mcp_init();

	parent = (struct pthread_tcb*)current_uthread;
	ret = posix_memalign((void**)&pthread, __alignof__(struct pthread_tcb),
	                     sizeof(struct pthread_tcb));
	assert(!ret);
	memset(pthread, 0, sizeof(struct pthread_tcb));	/* aggressively 0 for bugs*/
	pthread->stacksize = PTHREAD_STACK_SIZE;	/* default */
	pthread->state = PTH_CREATED;
	pthread->id = get_next_pid();
	/* Might override these later, based on attr && EXPLICIT_SCHED */
	pthread->sched_policy = parent->sched_policy;
	pthread->sched_priority = parent->sched_priority;
	SLIST_INIT(&pthread->cr_stack);
	/* Respect the attributes */
	if (attr) {
		if (attr->stacksize)					/* don't set a 0 stacksize */
			pthread->stacksize = attr->stacksize;
		if (attr->detachstate == PTHREAD_CREATE_DETACHED)
			uth_attr.detached = TRUE;
		if (attr->sched_inherit == PTHREAD_EXPLICIT_SCHED) {
			pthread->sched_policy = attr->sched_policy;
			pthread->sched_priority = attr->sched_priority;
		}
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
	TAILQ_REMOVE(&active_queue, pthread, tq_next);
	mcs_pdr_unlock(&queue_lock);
}

int pthread_join(struct pthread_tcb *join_target, void **retval)
{
	uthread_join((struct uthread*)join_target, retval);
	return 0;
}

static inline void pthread_exit_no_cleanup(void *ret)
{
	struct pthread_tcb *pthread = pthread_self();

	while (SLIST_FIRST(&pthread->cr_stack))
		pthread_cleanup_pop(FALSE);
	destroy_dtls();
	uth_2ls_thread_exit(ret);
}

void pthread_exit(void *ret)
{
	struct pthread_tcb *pthread = pthread_self();
	while (SLIST_FIRST(&pthread->cr_stack))
		pthread_cleanup_pop(TRUE);
	pthread_exit_no_cleanup(ret);
}

/* Cooperative yielding of the processor, to allow other threads to run */
int pthread_yield(void)
{
	uthread_sched_yield();
	return 0;
}

int pthread_cancel(pthread_t __th)
{
	fprintf(stderr, "Unsupported %s!", __FUNCTION__);
	abort();
	return -1;
}

void pthread_cleanup_push(void (*routine)(void *), void *arg)
{
	struct pthread_tcb *p = pthread_self();
	struct pthread_cleanup_routine *r = malloc(sizeof(*r));
	r->routine = routine;
	r->arg = arg;
	SLIST_INSERT_HEAD(&p->cr_stack, r, cr_next);
}

void pthread_cleanup_pop(int execute)
{
	struct pthread_tcb *p = pthread_self();
	struct pthread_cleanup_routine *r = SLIST_FIRST(&p->cr_stack);
	if (r) {
		SLIST_REMOVE_HEAD(&p->cr_stack, cr_next);
		if (execute)
			r->routine(r->arg);
		free(r);
	}
}

int pthread_mutexattr_init(pthread_mutexattr_t *attr)
{
	attr->type = PTHREAD_MUTEX_DEFAULT;
	return 0;
}

int pthread_mutexattr_destroy(pthread_mutexattr_t *attr)
{
	return 0;
}

int pthread_attr_setdetachstate(pthread_attr_t *__attr, int __detachstate)
{
	__attr->detachstate = __detachstate;
	return 0;
}

int pthread_mutexattr_gettype(const pthread_mutexattr_t *attr, int *type)
{
	*type = attr ? attr->type : PTHREAD_MUTEX_DEFAULT;
	return 0;
}

static bool __pthread_mutex_type_ok(int type)
{
	switch (type) {
	case PTHREAD_MUTEX_NORMAL:
	case PTHREAD_MUTEX_RECURSIVE:
		return TRUE;
	}
	return FALSE;
}

int pthread_mutexattr_settype(pthread_mutexattr_t *attr, int type)
{
	if (!__pthread_mutex_type_ok(type))
		return EINVAL;
	attr->type = type;
	return 0;
}

int pthread_mutex_init(pthread_mutex_t *m, const pthread_mutexattr_t *attr)
{
	if (!__pthread_mutex_type_ok(attr->type))
		return EINVAL;
	m->type = attr->type;
	switch (m->type) {
	case PTHREAD_MUTEX_NORMAL:
		uth_mutex_init(&m->mtx);
		break;
	case PTHREAD_MUTEX_RECURSIVE:
		uth_recurse_mutex_init(&m->r_mtx);
		break;
	}
	return 0;
}

int pthread_mutex_lock(pthread_mutex_t *m)
{
	switch (m->type) {
	case PTHREAD_MUTEX_NORMAL:
		uth_mutex_lock(&m->mtx);
		break;
	case PTHREAD_MUTEX_RECURSIVE:
		uth_recurse_mutex_lock(&m->r_mtx);
		break;
	default:
		panic("Bad pth mutex type %d!", m->type);
	}
	return 0;
}

int pthread_mutex_trylock(pthread_mutex_t *m)
{
	bool got_it;

	switch (m->type) {
	case PTHREAD_MUTEX_NORMAL:
		got_it = uth_mutex_trylock(&m->mtx);
		break;
	case PTHREAD_MUTEX_RECURSIVE:
		got_it = uth_recurse_mutex_trylock(&m->r_mtx);
		break;
	default:
		panic("Bad pth mutex type %d!", m->type);
	}
	return got_it ? 0 : EBUSY;
}

int pthread_mutex_unlock(pthread_mutex_t *m)
{
	switch (m->type) {
	case PTHREAD_MUTEX_NORMAL:
		uth_mutex_unlock(&m->mtx);
		break;
	case PTHREAD_MUTEX_RECURSIVE:
		uth_recurse_mutex_unlock(&m->r_mtx);
		break;
	default:
		panic("Bad pth mutex type %d!", m->type);
	}
	return 0;
}

int pthread_mutex_destroy(pthread_mutex_t *m)
{
	switch (m->type) {
	case PTHREAD_MUTEX_NORMAL:
		uth_mutex_destroy(&m->mtx);
		break;
	case PTHREAD_MUTEX_RECURSIVE:
		uth_recurse_mutex_destroy(&m->r_mtx);
		break;
	default:
		panic("Bad pth mutex type %d!", m->type);
	}
	return 0;
}

int pthread_mutex_timedlock(pthread_mutex_t *m, const struct timespec *abstime)
{
	bool got_it;

	switch (m->type) {
	case PTHREAD_MUTEX_NORMAL:
		got_it = uth_mutex_timed_lock(&m->mtx, abstime);
		break;
	case PTHREAD_MUTEX_RECURSIVE:
		got_it = uth_recurse_mutex_timed_lock(&m->r_mtx, abstime);
		break;
	default:
		panic("Bad pth mutex type %d!", m->type);
	}
	return got_it ? 0 : ETIMEDOUT;
}

int pthread_cond_init(pthread_cond_t *c, const pthread_condattr_t *a)
{
	if (a) {
		if (a->pshared != PTHREAD_PROCESS_PRIVATE)
			fprintf(stderr, "pthreads only supports private condvars");
		/* We also ignore clock_id */
	}
	uth_cond_var_init(c);
	return 0;
}

int pthread_cond_destroy(pthread_cond_t *c)
{
	uth_cond_var_destroy(c);
	return 0;
}

int pthread_cond_broadcast(pthread_cond_t *c)
{
	uth_cond_var_broadcast(c);
	return 0;
}

/* spec says this needs to work regardless of whether or not it holds the mutex
 * already. */
int pthread_cond_signal(pthread_cond_t *c)
{
	uth_cond_var_signal(c);
	return 0;
}

int pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m)
{
	switch (m->type) {
	case PTHREAD_MUTEX_NORMAL:
		uth_cond_var_wait(c, &m->mtx);
		break;
	case PTHREAD_MUTEX_RECURSIVE:
		uth_cond_var_wait_recurse(c, &m->r_mtx);
		break;
	default:
		panic("Bad pth mutex type %d!", m->type);
	}
	return 0;
}

int pthread_cond_timedwait(pthread_cond_t *c, pthread_mutex_t *m,
                           const struct timespec *abstime)
{
	bool got_it;

	switch (m->type) {
	case PTHREAD_MUTEX_NORMAL:
		got_it = uth_cond_var_timed_wait(c, &m->mtx, abstime);
		break;
	case PTHREAD_MUTEX_RECURSIVE:
		got_it = uth_cond_var_timed_wait_recurse(c, &m->r_mtx, abstime);
		break;
	default:
		panic("Bad pth mutex type %d!", m->type);
	}
	return got_it ? 0 : ETIMEDOUT;
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
	return 0;
}

int pthread_condattr_setclock(pthread_condattr_t *attr, clockid_t clock_id)
{
	printf("Warning: we don't do pthread condvar clock stuff\n");
	attr->clock = clock_id;
	return 0;
}

int pthread_rwlock_init(pthread_rwlock_t *rwl, const pthread_rwlockattr_t *a)
{
	uth_rwlock_init(rwl);
	return 0;
}

int pthread_rwlock_destroy(pthread_rwlock_t *rwl)
{
	uth_rwlock_destroy(rwl);
	return 0;
}

int pthread_rwlock_rdlock(pthread_rwlock_t *rwl)
{
	uth_rwlock_rdlock(rwl);
	return 0;
}

int pthread_rwlock_tryrdlock(pthread_rwlock_t *rwl)
{
	return uth_rwlock_try_rdlock(rwl) ? 0 : EBUSY;
}

int pthread_rwlock_wrlock(pthread_rwlock_t *rwl)
{
	uth_rwlock_wrlock(rwl);
	return 0;
}

int pthread_rwlock_trywrlock(pthread_rwlock_t *rwl)
{
	return uth_rwlock_try_wrlock(rwl) ? 0 : EBUSY;
}

int pthread_rwlock_unlock(pthread_rwlock_t *rwl)
{
	uth_rwlock_unlock(rwl);
	return 0;
}

pthread_t pthread_self(void)
{
	return (struct pthread_tcb*)uthread_self();
}

int pthread_equal(pthread_t t1, pthread_t t2)
{
  return t1 == t2;
}

int pthread_once(pthread_once_t *once_control, void (*init_routine)(void))
{
	/* pthread_once's init routine doesn't take an argument, like parlibs.  This
	 * means the func will be run with an argument passed to it, but it'll be
	 * ignored. */
	parlib_run_once(once_control, (void (*)(void *))init_routine, NULL);
	/* The return for pthread_once isn't an error from the function, it's just
	 * an overall error.  Note pthread's init_routine() has no return value. */
	return 0;
}

int pthread_barrier_init(pthread_barrier_t *b,
                         const pthread_barrierattr_t *a, int count)
{
	b->total_threads = count;
	b->sense = 0;
	atomic_set(&b->count, count);
	spin_pdr_init(&b->lock);
	__uth_sync_init(&b->waiters);
	b->nr_waiters = 0;
	return 0;
}

struct barrier_junk {
	pthread_barrier_t				*b;
	int								ls;
};

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
	return (*state)++ % PTHREAD_BARRIER_SPINS;
}

/* Callback/bottom half of barrier. */
static void __pth_barrier_cb(struct uthread *uthread, void *junk)
{
	pthread_barrier_t *b = ((struct barrier_junk*)junk)->b;
	int ls = ((struct barrier_junk*)junk)->ls;

	uthread_has_blocked(uthread, UTH_EXT_BLK_MUTEX);
	/* TODO: if we used a trylock, we could bail as soon as we see sense */
	spin_pdr_lock(&b->lock);
	/* If sense is ls (our free value), we lost the race and shouldn't sleep */
	if (b->sense == ls) {
		spin_pdr_unlock(&b->lock);
		uthread_runnable(uthread);
		return;
	}
	/* otherwise, we sleep */
	__uth_sync_enqueue(uthread, &b->waiters);
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
	uth_sync_t restartees;
	struct uthread *uth_i;
	struct barrier_junk local_junk;
	
	long old_count = atomic_fetch_and_add(&b->count, -1);

	if (old_count == 1) {
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
		__uth_sync_init(&restartees);
		__uth_sync_swap(&restartees, &b->waiters);
		b->nr_waiters = 0;
		spin_pdr_unlock(&b->lock);
		__uth_sync_wake_all(&restartees);
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
	assert(!b->nr_waiters);
	__uth_sync_destroy(&b->waiters);
	/* Free any locks (if we end up using an MCS) */
	return 0;
}

int pthread_detach(pthread_t thread)
{
	uthread_detach((struct uthread*)thread);
	return 0;
}

int pthread_kill(pthread_t thread, int signo)
{
	return uthread_signal(&thread->uthread, signo);
}

int pthread_sigmask(int how, const sigset_t *set, sigset_t *oset)
{
	int ret = sigprocmask(how, set, oset);

	/* Ensures any pending signals we just unmasked get processed. */
	if (set && ret == 0)
		pthread_yield();
	return ret;
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


/* Scheduling Stuff */

static bool policy_is_supported(int policy)
{
	/* As our scheduler changes, we can add more policies here */
	switch (policy) {
		case SCHED_FIFO:
			return TRUE;
		default:
			return FALSE;
	}
}

int pthread_attr_setschedparam(pthread_attr_t *attr,
                               const struct sched_param *param)
{
	/* The set of acceptable priorities are based on the scheduling policy.
	 * We'll just accept any old number, since we might not know the policy
	 * yet.  I didn't see anything in the man pages saying attr had to have a
	 * policy set before setting priority. */
	attr->sched_priority = param->sched_priority;
	return 0;
}

int pthread_attr_getschedparam(pthread_attr_t *attr,
                               struct sched_param *param)
{
	param->sched_priority = attr->sched_priority;
	return 0;
}

int pthread_attr_setschedpolicy(pthread_attr_t *attr, int policy)
{
	if (!policy_is_supported(policy))
		return -EINVAL;
	attr->sched_policy = policy;
	return 0;
}

int pthread_attr_getschedpolicy(pthread_attr_t *attr, int *policy)
{
	*policy = attr->sched_policy;
	return 0;
}

/* We only support SCOPE_PROCESS, so we don't even use the attr. */
int pthread_attr_setscope(pthread_attr_t *attr, int scope)
{
	if (scope != PTHREAD_SCOPE_PROCESS)
		return -ENOTSUP;
	return 0;
}

int pthread_attr_getscope(pthread_attr_t *attr, int *scope)
{
	*scope = PTHREAD_SCOPE_PROCESS;
	return 0;
}

/* Inheritance refers to policy, priority, scope */
int pthread_attr_setinheritsched(pthread_attr_t *attr,
                                 int inheritsched)
{
	switch (inheritsched) {
		case PTHREAD_INHERIT_SCHED:
		case PTHREAD_EXPLICIT_SCHED:
			break;
		default:
			return -EINVAL;
	}
	attr->sched_inherit = inheritsched;
	return 0;
}

int pthread_attr_getinheritsched(const pthread_attr_t *attr,
                                 int *inheritsched)
{
	*inheritsched = attr->sched_inherit;
	return 0;
}

int pthread_setschedparam(pthread_t thread, int policy,
                           const struct sched_param *param)
{
	if (!policy_is_supported(policy))
		return -EINVAL;
	thread->sched_policy = policy;
	/* We actually could check if the priority falls in the range of the
	 * specified policy here, since we have both policy and priority. */
	thread->sched_priority = param->sched_priority;
	return 0;
}

int pthread_getschedparam(pthread_t thread, int *policy,
                           struct sched_param *param)
{
	*policy = thread->sched_policy;
	param->sched_priority = thread->sched_priority;
	return 0;
}
