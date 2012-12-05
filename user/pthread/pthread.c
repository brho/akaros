#include <ros/arch/trapframe.h>
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
#include <assert.h>
#include <event.h>
#include <ucq.h>

struct pthread_queue ready_queue = TAILQ_HEAD_INITIALIZER(ready_queue);
struct pthread_queue active_queue = TAILQ_HEAD_INITIALIZER(active_queue);
struct mcs_pdr_lock queue_lock;
pthread_once_t init_once = PTHREAD_ONCE_INIT;
int threads_ready = 0;
int threads_active = 0;
bool can_adjust_vcores = TRUE;

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
void pth_preempt_pending(void);
void pth_spawn_thread(uintptr_t pc_start, void *data);

/* Event Handlers */
static void pth_handle_syscall(struct event_msg *ev_msg, unsigned int ev_type);

struct schedule_ops pthread_sched_ops = {
	pth_sched_entry,
	pth_thread_runnable,
	pth_thread_paused,
	pth_thread_blockon_sysc,
	pth_thread_has_blocked,
	0, /* pth_preempt_pending, */
	0, /* pth_spawn_thread, */
};

/* Publish our sched_ops, overriding the weak defaults */
struct schedule_ops *sched_ops = &pthread_sched_ops;

/* Static helpers */
static void __pthread_free_stack(struct pthread_tcb *pt);
static int __pthread_allocate_stack(struct pthread_tcb *pt);

/* Called from vcore entry.  Options usually include restarting whoever was
 * running there before or running a new thread.  Events are handled out of
 * event.c (table of function pointers, stuff like that). */
void __attribute__((noreturn)) pth_sched_entry(void)
{
	uint32_t vcoreid = vcore_id();
	if (current_uthread) {
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
	run_uthread((struct uthread*)new_thread);
	assert(0);
}

/* Could move this, along with start_routine and arg, into the 2LSs */
static void __pthread_run(void)
{
	struct pthread_tcb *me = pthread_self();
	pthread_exit(me->start_routine(me->arg));
}

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
	uthread_runnable(uthread);
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
	uthread_runnable(ut_restartee);
}

/* This handler is usually run in vcore context, though I can imagine it being
 * called by a uthread in some other threading library. */
static void pth_handle_syscall(struct event_msg *ev_msg, unsigned int ev_type)
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
	bool need_to_restart = FALSE;
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
	assert(!munmap(pt->stacktop - pt->stacksize, pt->stacksize));
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

/* Do whatever init you want.  At some point call uthread_lib_init() and pass it
 * a uthread representing thread0 (int main()) */
int pthread_lib_init(void)
{
	/* Make sure this only initializes once */
	static bool initialized = FALSE;
	if (initialized)
		return 0;
	initialized = TRUE;
	uintptr_t mmap_block;
	mcs_pdr_init(&queue_lock);
	/* Create a pthread_tcb for the main thread */
	pthread_t t = (pthread_t)calloc(1, sizeof(struct pthread_tcb));
	assert(t);
	t->id = get_next_pid();
	t->stacksize = USTACK_NUM_PAGES * PGSIZE;
	t->stacktop = (void*)USTACKTOP;
	t->detached = TRUE;
	t->state = PTH_RUNNING;
	t->joiner = 0;
	assert(t->id == 0);
	/* Put the new pthread (thread0) on the active queue */
	mcs_pdr_lock(&queue_lock);	/* arguably, we don't need these (_S mode) */
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
	ev_handlers[EV_SYSCALL] = pth_handle_syscall;
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
	 * big deal one way or the other.  Note that vcore_init() hasn't happened
	 * yet, so if a 2LS somehow wants to have its init stuff use things like
	 * vcore stacks or TLSs, we'll need to change this. */
	assert(!uthread_lib_init((struct uthread*)t));
	return 0;
}

int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start_routine)(void *), void *arg)
{
	static bool first = TRUE;
	if (first) {
		assert(!pthread_lib_init());
		first = FALSE;
	}
	/* Create the actual thread */
	struct pthread_tcb *pthread;
	pthread = (pthread_t)calloc(1, sizeof(struct pthread_tcb));
	assert(pthread);
	pthread->stacksize = PTHREAD_STACK_SIZE;	/* default */
	pthread->state = PTH_CREATED;
	pthread->id = get_next_pid();
	pthread->detached = FALSE;				/* default */
	pthread->joiner = 0;
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
	init_user_tf(&pthread->uthread.utf, (long)&__pthread_run,
	             (long)(pthread->stacktop));
	pthread->start_routine = start_routine;
	pthread->arg = arg;
	/* Initialize the uthread */
	uthread_init((struct uthread*)pthread);
	uthread_runnable((struct uthread*)pthread);
	*thread = pthread;
	return 0;
}

/* Helper that all pthread-controlled yield paths call.  Just does some
 * accounting.  This is another example of how the much-loathed (and loved)
 * active queue is keeping us honest. */
static void __pthread_generic_yield(struct pthread_tcb *pthread)
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
		uthread_runnable(uthread);	/* wake ourselves */
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
			uthread_runnable((struct uthread*)temp_pth);
		}
	}
}

void pthread_exit(void *ret)
{
	struct pthread_tcb *pthread = pthread_self();
	pthread->retval = ret;
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
	uthread_runnable(uthread);
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
  c->attr = a;
  memset(c->waiters,0,sizeof(c->waiters));
  memset(c->in_use,0,sizeof(c->in_use));
  c->next_waiter = 0;
  return 0;
}

int pthread_cond_destroy(pthread_cond_t *c)
{
  return 0;
}

int pthread_cond_broadcast(pthread_cond_t *c)
{
  memset(c->waiters,0,sizeof(c->waiters));
  return 0;
}

int pthread_cond_signal(pthread_cond_t *c)
{
  int i;
  for(i = 0; i < MAX_PTHREADS; i++)
  {
    if(c->waiters[i])
    {
      c->waiters[i] = 0;
      break;
    }
  }
  return 0;
}

int pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m)
{
  uint32_t old_waiter = c->next_waiter;
  uint32_t my_waiter = c->next_waiter;
  
  //allocate a slot
  while (atomic_swap_u32(& (c->in_use[my_waiter]), SLOT_IN_USE) == SLOT_IN_USE)
  {
    my_waiter = (my_waiter + 1) % MAX_PTHREADS;
    assert (old_waiter != my_waiter);  // do not want to wrap around
  }
  c->waiters[my_waiter] = WAITER_WAITING;
  c->next_waiter = (my_waiter+1) % MAX_PTHREADS;  // race on next_waiter but ok, because it is advisary

  pthread_mutex_unlock(m);

  volatile int* poll = &c->waiters[my_waiter];
  while(*poll);
  c->in_use[my_waiter] = SLOT_FREE;
  pthread_mutex_lock(m);

  return 0;
}

int pthread_condattr_init(pthread_condattr_t *a)
{
  a = PTHREAD_PROCESS_PRIVATE;
  return 0;
}

int pthread_condattr_destroy(pthread_condattr_t *a)
{
  return 0;
}

int pthread_condattr_setpshared(pthread_condattr_t *a, int s)
{
  a->pshared = s;
  return 0;
}

int pthread_condattr_getpshared(pthread_condattr_t *a, int *s)
{
  *s = a->pshared;
  return 0;
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

int pthread_barrier_init(pthread_barrier_t* b, const pthread_barrierattr_t* a, int count)
{
  b->nprocs = b->count = count;
  b->sense = 0;
  pthread_mutex_init(&b->pmutex, 0);
  return 0;
}

int pthread_barrier_wait(pthread_barrier_t* b)
{
  unsigned int spinner = 0;
  int ls = !b->sense;

  pthread_mutex_lock(&b->pmutex);
  int count = --b->count;
  pthread_mutex_unlock(&b->pmutex);

  if(count == 0)
  {
    printd("Thread %d is last to hit the barrier, resetting...\n", pthread_self()->id);
    b->count = b->nprocs;
	wmb();
    b->sense = ls;
    return PTHREAD_BARRIER_SERIAL_THREAD;
  }
  else
  {
    while(b->sense != ls) {
      cpu_relax();
      spin_to_sleep(PTHREAD_BARRIER_SPINS, &spinner);
    }
    return 0;
  }
}

int pthread_barrier_destroy(pthread_barrier_t* b)
{
  pthread_mutex_destroy(&b->pmutex);
  return 0;
}

int pthread_detach(pthread_t thread)
{
	/* TODO: race on this state.  Someone could be trying to join now */
	thread->detached = TRUE;
	return 0;
}

int pthread_kill (pthread_t __threadid, int __signo)
{
	printf("pthread_kill is not yet implemented!");
	return -1;
}


int pthread_sigmask(int how, const sigset_t *set, sigset_t *oset)
{
	printf("pthread_sigmask is not yet implemented!");
	return -1;
}

int pthread_sigqueue(pthread_t *thread, int sig, const union sigval value)
{
	printf("pthread_sigqueue is not yet implemented!");
	return -1;
}

