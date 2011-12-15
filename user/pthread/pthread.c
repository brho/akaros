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

/* Array of per-vcore structs to manage waiting on syscalls and handling
 * overflow.  Init'd in pth_init(). */
struct sysc_mgmt *sysc_mgmt = 0;

/* Helper / local functions */
static int get_next_pid(void);
static inline void spin_to_sleep(unsigned int spins, unsigned int *spun);

/* Pthread 2LS operations */
void pth_sched_entry(void);
void pth_thread_runnable(struct uthread *uthread);
void pth_thread_yield(struct uthread *uthread);
void pth_thread_paused(struct uthread *uthread);
void pth_preempt_pending(void);
void pth_spawn_thread(uintptr_t pc_start, void *data);
void pth_blockon_sysc(struct syscall *sysc);

/* Event Handlers */
static void pth_handle_syscall(struct event_msg *ev_msg, unsigned int ev_type);

struct schedule_ops pthread_sched_ops = {
	pth_sched_entry,
	pth_thread_runnable,
	pth_thread_yield,
	pth_thread_paused,
	pth_blockon_sysc,
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
		vcore_yield(FALSE);
	} while (1);
	assert(((struct uthread*)new_thread)->state != UT_RUNNING);
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
	/* Insert the newly created thread into the ready queue of threads.
	 * It will be removed from this queue later when vcore_entry() comes up */
	mcs_pdr_lock(&queue_lock);
	TAILQ_INSERT_TAIL(&ready_queue, pthread, next);
	threads_ready++;
	mcs_pdr_unlock(&queue_lock);
	/* Smarter schedulers should look at the num_vcores() and how much work is
	 * going on to make a decision about how many vcores to request. */
	vcore_request(threads_ready);
}

/* The calling thread is yielding.  Do what you need to do to restart (like put
 * yourself on a runqueue), or do some accounting.  Eventually, this might be a
 * little more generic than just yield. */
void pth_thread_yield(struct uthread *uthread)
{
	struct pthread_tcb *pthread = (struct pthread_tcb*)uthread;
	struct pthread_tcb *temp_pth = 0;	/* used for exiting AND joining */
	/* Remove from the active list, whether exiting or yielding. */
	mcs_pdr_lock(&queue_lock);
	threads_active--;
	TAILQ_REMOVE(&active_queue, pthread, next);
	mcs_pdr_unlock(&queue_lock);
	if (pthread->flags & PTHREAD_EXITING) {
		/* Destroy the pthread */
		uthread_cleanup(uthread);
		/* Cleanup, mirroring pthread_create() */
		__pthread_free_stack(pthread);
		/* TODO: race on detach state */
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
	} else if (pthread->flags & PTHREAD_JOINING) {
		/* We're trying to join, yield til we get woken up */
		/* put ourselves in the join target's joiner slot.  If we get anything
		 * back, we lost the race and need to wake ourselves. */
		temp_pth = atomic_swap_ptr((void**)&pthread->join_target->joiner,
		                           pthread);
		/* after that atomic swap, the pthread might be woken up (if it
		 * succeeded), so don't touch pthread again after that (this following
		 * if () is okay). */
		if (temp_pth) {
			assert(temp_pth == pthread->join_target);	/* Sanity */
			/* wake ourselves, not the exited one! */
			printd("[pth] %08p already exit, rewaking ourselves, joiner %08p\n",
			       temp_pth, pthread);
			pth_thread_runnable((struct uthread*)pthread);
		}
	} else {
		/* Yielding for no apparent reason (being nice / help break deadlocks).
		 * Just wake it up and make it ready again. */
		pth_thread_runnable((struct uthread*)pthread);
	}
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
	/* At this point, you could do something clever, like put it at the front of
	 * the runqueue, see if it was holding a lock, do some accounting, or
	 * whatever. */
	uthread_runnable(uthread);
}

void pth_preempt_pending(void)
{
}

void pth_spawn_thread(uintptr_t pc_start, void *data)
{
}

/* Restarts a uthread hanging off a syscall.  For the simple pthread case, we
 * just make it runnable and let the main scheduler code handle it. */
static void restart_thread(struct syscall *sysc)
{
	struct uthread *ut_restartee = (struct uthread*)sysc->u_data;
	/* uthread stuff here: */
	assert(ut_restartee);
	assert(ut_restartee->state == UT_BLOCKED);
	assert(ut_restartee->sysc == sysc);
	ut_restartee->sysc = 0;	/* so we don't 'reblock' on this later */
	uthread_runnable(ut_restartee);
}

/* This handler is usually run in vcore context, though I can imagine it being
 * called by a uthread in some other threading library. */
static void pth_handle_syscall(struct event_msg *ev_msg, unsigned int ev_type)
{
	struct syscall *sysc;
	assert(in_vcore_context());
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
void pth_blockon_sysc(struct syscall *sysc)
{
	int old_flags;
	bool need_to_restart = FALSE;
	uint32_t vcoreid = vcore_id();

	assert(current_uthread->state == UT_BLOCKED);
	/* rip from the active queue */
	struct pthread_tcb *pthread = (struct pthread_tcb*)current_uthread;
	mcs_pdr_lock(&queue_lock);
	threads_active--;
	TAILQ_REMOVE(&active_queue, pthread, next);
	mcs_pdr_unlock(&queue_lock);

	/* Set things up so we can wake this thread up later */
	sysc->u_data = current_uthread;
	/* Register our vcore's syscall ev_q to hear about this syscall. */
	if (!register_evq(sysc, sysc_mgmt[vcoreid].ev_q)) {
		/* Lost the race with the call being done.  The kernel won't send the
		 * event.  Just restart him. */
		restart_thread(sysc);
	}
	/* GIANT WARNING: do not touch the thread after this point. */
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
static int pthread_lib_init(void)
{
	/* Make sure this only runs once */
	static bool initialized = FALSE;
	if (initialized)
		return -1;
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
	t->flags = 0;
	t->join_target = 0;
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
	pthread->flags = 0;
	pthread->id = get_next_pid();
	pthread->detached = FALSE;				/* default */
	pthread->join_target = 0;
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

int pthread_join(pthread_t thread, void** retval)
{
	struct pthread_tcb *caller = (struct pthread_tcb*)current_uthread;
	/* Not sure if this is the right semantics.  There is a race if we deref
	 * thread and he is already freed (which would have happened if he was
	 * detached. */
	if (thread->detached) {
		printf("[pthread] trying to join on a detached pthread");
		return -1;
	}
	/* See if it is already done, to avoid the pain of a uthread_yield() (the
	 * early check is an optimization, pth_thread_yield() handles the race). */
	if (!thread->joiner) {
		/* Time to join, set things up so pth_thread_yield() knows what to do */
		caller->flags |= PTHREAD_JOINING;
		caller->join_target = thread;
		uthread_yield(TRUE);
		/* When we return/restart, the thread will be done */
	} else {
		assert(thread->joiner == thread);	/* sanity check */
	}
	if (retval)
		*retval = thread->retval;
	free(thread);
	return 0;
}

int pthread_yield(void)
{
	uthread_yield(TRUE);
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

/* This function cannot be migrated to a different vcore by the userspace
 * scheduler.  Will need to sort that shit out. */
void pthread_exit(void *ret)
{
	struct pthread_tcb *pthread = pthread_self();
	pthread->retval = ret;
	/* So our pth_thread_yield knows we want to exit */
	pthread->flags |= PTHREAD_EXITING;
	uthread_yield(FALSE);
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
