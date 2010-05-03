#include <ros/arch/trapframe.h>
#include <pthread.h>
#include <vcore.h>
#include <mcs.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <rstdio.h>
#include <errno.h>
#include <parlib.h>
#include <ros/notification.h>
#include <arch/atomic.h>
#include <arch/arch.h>
#include <sys/queue.h>
#include <sys/mman.h>
#include <assert.h>

struct pthread_queue ready_queue = TAILQ_HEAD_INITIALIZER(ready_queue);
struct pthread_queue active_queue = TAILQ_HEAD_INITIALIZER(active_queue);
mcs_lock_t queue_lock = MCS_LOCK_INIT;
pthread_once_t init_once = PTHREAD_ONCE_INIT;
int threads_ready = 0;
int threads_active = 0;

/* Helper / local functions */
static int get_next_pid(void);
static inline void spin_to_sleep(unsigned int spins, unsigned int *spun);

__thread struct pthread_tcb *current_thread = 0;

void _pthread_init()
{
	if (vcore_init())
		printf("vcore_init() failed, we're fucked!\n");
	
	assert(vcore_id() == 0);

	/* tell the kernel where and how we want to receive notifications */
	struct notif_method *nm;
	for (int i = 0; i < MAX_NR_NOTIF; i++) {
		nm = &__procdata.notif_methods[i];
		nm->flags |= NOTIF_WANTED | NOTIF_MSG | NOTIF_IPI;
		nm->vcoreid = i % 2; // vcore0 or 1, keepin' it fresh.
	}
	/* don't forget to enable notifs on vcore0.  if you don't, the kernel will
	 * restart your _S with notifs disabled, which is a path to confusion. */
	struct preempt_data *vcpd = &__procdata.vcore_preempt_data[0];
	vcpd->notif_enabled = TRUE;

	/* Create a pthread_tcb for the main thread */
	pthread_t t = (pthread_t)calloc(1, sizeof(struct pthread_tcb));
	t->id = get_next_pid();
	assert(t->id == 0);
	/* Put the new pthread on the active queue */
	mcs_lock_lock(&queue_lock);
	threads_active++;
	TAILQ_INSERT_TAIL(&active_queue, t, next);
	mcs_lock_unlock(&queue_lock);
	
	/* Save a pointer to the newly created threads tls region into its tcb */
	t->tls_desc = get_tls_desc(0);
	/* Save a pointer to the pthread in its own TLS */
	current_thread = t;

	/* Change temporarily to vcore0s tls region so we can save the newly created
	 * tcb into its current_thread variable and then restore it.  One minor
	 * issue is that vcore0's transition-TLS isn't TLS_INITed yet.  Until it is
	 * (right before vcore_entry(), don't try and take the address of any of
	 * its TLS vars. */
	extern void** vcore_thread_control_blocks;
	set_tls_desc(vcore_thread_control_blocks[0], 0);
	current_thread = t;
	set_tls_desc(t->tls_desc, 0);

	// TODO: consider replacing this when we have an interface allowing
	// requesting absolute num vcores, and moving it to pthread_create and
	// asking for 2
	vcore_request(1);
}

void __attribute__((noreturn)) vcore_entry()
{
	uint32_t vcoreid = vcore_id();

	struct preempt_data *vcpd = &__procdata.vcore_preempt_data[vcoreid];
	struct vcore *vc = &__procinfo.vcoremap[vcoreid];

	/* Should always have notifications disabled when coming in here. */
	assert(vcpd->notif_enabled == FALSE);

	/* Put this in the loop that deals with notifications.  It will return if
	 * there is no preempt pending. */ 
	// TODO: prob make a handle_notif() function
	if (vc->preempt_pending)
		sys_yield(TRUE);
	// TODO: consider making this restart path work for restarting as well as
	// freshly starting
	if (current_thread) {
		vcpd->notif_pending = 0;
		/* Do one last check for notifs after clearing pending */
		// TODO: call the handle_notif() here (first)

		set_tls_desc(current_thread->tls_desc, vcoreid);
		/* Pop the user trap frame */
		pop_ros_tf(&vcpd->notif_tf, vcoreid);
		assert(0);
	}

	/* no one currently running, so lets get someone from the ready queue */
	struct pthread_tcb *new_thread = NULL;
#ifdef __CONFIG_OSDI__
	// Added so that we will return back to here if there is no new thread
	// instead of at the top of this function.  Related to the fact that
	// the kernel level scheduler can't yet handle scheduling manycore 
	// processed yet when there are no more jobs left (i.e. sys_yield() will
	// return instead of actually yielding...).
	while(!new_thread) {
#endif
		mcs_lock_lock(&queue_lock);
		new_thread = TAILQ_FIRST(&ready_queue);
		if (new_thread) {
			TAILQ_REMOVE(&ready_queue, new_thread, next);
			TAILQ_INSERT_TAIL(&active_queue, new_thread, next);
			threads_active++;
			threads_ready--;
		}
		mcs_lock_unlock(&queue_lock);
		if (!new_thread) {
			printd("[P] No threads, vcore %d is yielding\n", vcoreid);
			sys_yield(0);
		}
#ifdef __CONFIG_OSDI__
	}
#endif
	/* Save a ptr to the pthread running in the transition context's TLS */
	current_thread = new_thread;
	printd("[P] Vcore %d is starting pthread %d\n", vcoreid, new_thread->id);

	vcpd->notif_pending = 0;
	/* Do one last check for notifs after clearing pending */
	// TODO: call the handle_notif() here (first)
	set_tls_desc(new_thread->tls_desc, vcoreid);

	/* Load silly state (Floating point) too.  For real */
	// TODO: (HSS)

	/* Pop the user trap frame */
	pop_ros_tf(&new_thread->utf, vcoreid);
	assert(0);
}

int pthread_attr_init(pthread_attr_t *a)
{
  return 0;
}

int pthread_attr_destroy(pthread_attr_t *a)
{
  return 0;
}

static void __pthread_free_tls(struct pthread_tcb *pt)
{
	extern void _dl_deallocate_tls (void *tcb, bool dealloc_tcb) internal_function;

	assert(pt->tls_desc);
	_dl_deallocate_tls(pt->tls_desc, TRUE);
	pt->tls_desc = NULL;
}

static int __pthread_allocate_tls(struct pthread_tcb *pt)
{
	extern void *_dl_allocate_tls (void *mem) internal_function;

	assert(!pt->tls_desc);
	pt->tls_desc = _dl_allocate_tls(NULL);
	if (!pt->tls_desc) {
		errno = ENOMEM;
		return -1;
	}
	return 0;
}

// TODO: how big do we want these?  ideally, we want to be able to guard and map
// more space if we go too far.
#define PTHREAD_STACK_PAGES 4
#define PTHREAD_STACK_SIZE (PTHREAD_STACK_PAGES*PGSIZE)

static void __pthread_free_stack(struct pthread_tcb *pt)
{
	assert(!munmap(pt->stacktop - PTHREAD_STACK_SIZE, PTHREAD_STACK_SIZE));
}

static int __pthread_allocate_stack(struct pthread_tcb *pt)
{
	void* stackbot = mmap(0, PTHREAD_STACK_SIZE,
	                      PROT_READ|PROT_WRITE|PROT_EXEC,
	                      MAP_POPULATE|MAP_ANONYMOUS, -1, 0);
	if (stackbot == MAP_FAILED)
		return -1; // errno set by mmap
	pt->stacktop = stackbot + PTHREAD_STACK_SIZE;
	return 0;
}

void __pthread_run(void)
{
	struct pthread_tcb *me = current_thread;
	pthread_exit(me->start_routine(me->arg));
}

// Warning, this will reuse numbers eventually
static int get_next_pid(void)
{
	static uint32_t next_pid = 0;
	return next_pid++;
}

int pthread_create(pthread_t* thread, const pthread_attr_t* attr,
                   void *(*start_routine)(void *), void* arg)
{
	/* After this init, we are an MCP and the caller is a pthread */
	pthread_once(&init_once,&_pthread_init);

	struct pthread_tcb *t = pthread_self();
	assert(t); /* TODO/FYI: doesn't prevent this from being in vcore context */
	/* Don't migrate this thread to anothe vcore, since it depends on being on
	 * the same vcore throughout. */
	t->dont_migrate = TRUE;
	uint32_t vcoreid = vcore_id();
	*thread = (pthread_t)calloc(1, sizeof(struct pthread_tcb));
	(*thread)->start_routine = start_routine;
	(*thread)->arg = arg;
	(*thread)->id = get_next_pid();
	if (__pthread_allocate_stack(*thread) ||  __pthread_allocate_tls(*thread))
		printf("We're fucked\n");
	/* Save the ptr to the new pthread in that pthread's TLS */
	set_tls_desc((*thread)->tls_desc, vcoreid);
	current_thread = *thread;
	set_tls_desc(t->tls_desc, vcoreid);
	/* Set the u_tf to start up in __pthread_run, which will call the real
	 * start_routine and pass it the arg. */
	init_user_tf(&(*thread)->utf, (uint32_t)__pthread_run, 
                 (uint32_t)((*thread)->stacktop));
	/* Insert the newly created thread into the ready queue of threads.
	 * It will be removed from this queue later when vcore_entry() comes up */
	mcs_lock_lock(&queue_lock);
	TAILQ_INSERT_TAIL(&ready_queue, *thread, next);
	threads_ready++;
	mcs_lock_unlock(&queue_lock);
	/* Okay to migrate now. */
	t->dont_migrate = FALSE;
	/* Attempt to request a new core, may or may not get it... */
	vcore_request(1);
	return 0;
}

int pthread_join(pthread_t thread, void** retval)
{
	/* Not sure if this is the right semantics.  There is a race if we deref
	 * thread and he is already freed (which would have happened if he was
	 * detached. */
	if (thread->detached) {
		printf("[pthread] trying to join on a detached pthread");
		return -1;
	}
	while (!thread->finished)
		pthread_yield();
	if (retval)
		*retval = thread->retval;
	free(thread);
	return 0;
}

static void __attribute__((noinline, noreturn)) 
__pthread_yield(struct pthread_tcb *t)
{
	/* TODO: want to set this to FALSE once we no longer depend on being on this
	 * vcore.  Though if we are using TLS, we are depending on the vcore.  Since
	 * notifs are disabled and we are in a transition context, we probably
	 * shouldn't be moved anyway.  It does mean that a pthread could get jammed.
	 * If we do this after putting it on the active list, we'll have a race on
	 * dont_migrate. */
	t->dont_migrate = FALSE;
	/* Take from the active list, and put on the ready list (tail).  Don't do
	 * this until we are done completely with the thread, since it can be
	 * restarted somewhere else. */
	mcs_lock_lock(&queue_lock);
	threads_active--;
	TAILQ_REMOVE(&active_queue, t, next);
	threads_ready++;
	TAILQ_INSERT_TAIL(&ready_queue, t, next);
	mcs_lock_unlock(&queue_lock);
	/* Leave the current vcore completely */
	current_thread = NULL; // this might be okay, even with a migration
	/* Go back to the entry point, where we can handle notifications or
	 * reschedule someone. */
	vcore_entry();
}

int pthread_yield(void)
{
	struct pthread_tcb *t = pthread_self();
	volatile bool yielding = TRUE; /* signal to short circuit when restarting */

	/* TODO: (HSS) Save silly state */
	// save_fp_state(&t->as);

	/* Don't migrate this thread to another vcore, since it depends on being on
	 * the same vcore throughout (once it disables notifs). */
	t->dont_migrate = TRUE;
	uint32_t vcoreid = vcore_id();
	printd("[P] Pthread id %d is yielding on vcore %d\n", t->id, vcoreid);
	struct preempt_data *vcpd = &__procdata.vcore_preempt_data[vcoreid];
	/* once we do this, we might miss a notif_pending, so we need to enter vcore
	 * entry later.  Need to disable notifs so we don't get in weird loops with
	 * save_ros_tf() and pop_ros_tf(). */
	vcpd->notif_enabled = FALSE;
	/* take the current state and save it into t->utf when this pthread
	 * restarts, it will continue from right after this, see yielding is false,
	 * and short ciruit the function. */
	save_ros_tf(&t->utf);
	if (!yielding)
		goto yield_return_path;
	yielding = FALSE; /* for when it starts back up */
	/* Change to the transition context (both TLS and stack). */
	extern void** vcore_thread_control_blocks;
	set_tls_desc(vcore_thread_control_blocks[vcoreid], vcoreid);
	assert(current_thread == t);	
	/* After this, make sure you don't use local variables.  Note the warning in
	 * pthread_exit() */
	set_stack_pointer((void*)vcpd->transition_stack);
	/* Finish exiting in another function. */
	__pthread_yield(current_thread);
	/* Should never get here */
	assert(0);
	/* Will jump here when the pthread's trapframe is restarted/popped. */
yield_return_path:
	printd("[P] pthread %d returning from a yield!\n", t->id);
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


int pthread_attr_setdetachstate(pthread_attr_t *__attr,int __detachstate) {
	*__attr = __detachstate;
	// TODO: the right thing
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
  m->lock = 0;
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
	return 0;
}

int pthread_mutex_trylock(pthread_mutex_t* m)
{
  return atomic_swap(&m->lock,1) == 0 ? 0 : EBUSY;
}

int pthread_mutex_unlock(pthread_mutex_t* m)
{
  /* Need to prevent the compiler (and some arches) from reordering older
   * stores */
  wmb();
  m->lock = 0;
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
  int old_waiter = c->next_waiter;
  int my_waiter = c->next_waiter;
  
  //allocate a slot
  while (atomic_swap (& (c->in_use[my_waiter]), SLOT_IN_USE) == SLOT_IN_USE)
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
  return current_thread;
}

int pthread_equal(pthread_t t1, pthread_t t2)
{
  return t1 == t2;
}

/* Need to have this as a separate, non-inlined function since we clobber the
 * stack pointer before calling it, and don't want the compiler to play games
 * with my hart. */
static void __attribute__((noinline, noreturn)) 
__pthread_exit(struct pthread_tcb *t)
{
	__pthread_free_tls(t);
	__pthread_free_stack(t);
	if (t->detached)
		free(t);
	/* Once we do this, our joiner can free us.  He won't free us if we're
	 * detached, but there is still a potential race there (since he's accessing
	 * someone who is freed. */
	t->finished = 1;
	current_thread = NULL;
	/* Go back to the entry point, where we can handle notifications or
	 * reschedule someone. */
	vcore_entry();
}

/* This function cannot be migrated to a different vcore by the userspace
 * scheduler.  Will need to sort that shit out.  */
void pthread_exit(void* ret)
{
	struct pthread_tcb *t = pthread_self();
	/* Don't migrate this thread to anothe vcore, since it depends on being on
	 * the same vcore throughout. */
	t->dont_migrate = TRUE; // won't set this to false later, since he is dying

	uint32_t vcoreid = vcore_id();
	struct preempt_data *vcpd = &__procdata.vcore_preempt_data[vcoreid];

	t->retval = ret;
	mcs_lock_lock(&queue_lock);
	threads_active--;
	TAILQ_REMOVE(&active_queue, t, next);
	mcs_lock_unlock(&queue_lock);

	printd("[P] Pthread id %d is exiting on vcore %d\n", t->id, vcoreid);
	
	/* once we do this, we might miss a notif_pending, so we need to enter vcore
	 * entry later. */
	vcpd->notif_enabled = FALSE;

	/* Change to the transition context (both TLS and stack). */
	extern void** vcore_thread_control_blocks;
	set_tls_desc(vcore_thread_control_blocks[vcoreid], vcoreid);
	assert(current_thread == t);	
	/* After this, make sure you don't use local variables.  Also, make sure the
	 * compiler doesn't use them without telling you (TODO).
	 *
	 * In each arch's set_stack_pointer, make sure you subtract off as much room
	 * as you need to any local vars that might be pushed before calling the
	 * next function, or for whatever other reason the compiler/hardware might
	 * walk up the stack a bit when calling a noreturn function. */
	set_stack_pointer((void*)vcpd->transition_stack);
	/* Finish exiting in another function.  Ugh. */
	__pthread_exit(current_thread);
}

int pthread_once(pthread_once_t* once_control, void (*init_routine)(void))
{
  if(atomic_swap(once_control,1) == 0)
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
