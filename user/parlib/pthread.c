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

	/* Create a pthread_tcb for the main thread */
	pthread_t t = (pthread_t)calloc(sizeof(struct pthread_tcb), 1);
	t->id = get_next_pid();
	assert(t->id == 0);
	
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

void vcore_entry()
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
		/* Do one last check for notifs before clearing pending */
		// TODO: call the handle_notif() here (first)
		vcpd->notif_pending = 0;
		set_tls_desc(current_thread->tls_desc, vcoreid);
		/* Pop the user trap frame */
		pop_ros_tf(&vcpd->notif_tf, vcoreid);
		assert(0);
	}

	/* no one currently running, so lets get someone from the ready queue */
	mcs_lock_lock(&queue_lock);
	struct pthread_tcb *new_thread = TAILQ_FIRST(&ready_queue);
	if (new_thread) {
		TAILQ_REMOVE(&ready_queue, new_thread, next);
		TAILQ_INSERT_TAIL(&active_queue, new_thread, next);
		threads_active++;
		threads_ready--;
	}
	mcs_lock_unlock(&queue_lock);
	if (!new_thread) {
		printf("[P] No threads, vcore %d is yielding\n", vcoreid);
		sys_yield(0);
	}
	/* Save a ptr to the pthread running in the transition context's TLS */
	current_thread = new_thread;

	/* Do one last check for notifs before clearing pending */
	// TODO: call the handle_notif() here (first)
	vcpd->notif_pending = 0;
	set_tls_desc(new_thread->tls_desc, vcoreid);

	/* Load silly state (Floating point) too.  For real */
	// TODO

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
	assert(t);
	/* Don't migrate this thread to anothe vcore, since it depends on being on
	 * the same vcore throughout. */
	t->dont_migrate = TRUE;

	uint32_t vcoreid = vcore_id();

	// Most fields already zeroed out by the calloc below...
	*thread = (pthread_t)calloc(sizeof(struct pthread_tcb), 1);
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

	// Insert the newly created thread into the ready queue of threads.
	// It will be removed from this queue later when vcore_entry() comes up
	mcs_lock_lock(&queue_lock);
	TAILQ_INSERT_TAIL(&ready_queue, *thread, next);
	threads_ready++;
	mcs_lock_unlock(&queue_lock);

	/* Okay to migrate now. */
	t->dont_migrate = FALSE;

	// Attempt to request a new core, may or may not get it...
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
	// TODO: something smarter than spinning (deschedule, etc)
	while(!thread->finished)
		cpu_relax(); // has a memory clobber
	if (retval)
		*retval = thread->retval;
	free(thread);
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

int pthread_mutex_lock(pthread_mutex_t* m)
{
  while(pthread_mutex_trylock(m))
    while(*(volatile size_t*)&m->lock);
  return 0;
}

int pthread_mutex_trylock(pthread_mutex_t* m)
{
  return atomic_swap(&m->lock,1) == 0 ? 0 : EBUSY;
}

int pthread_mutex_unlock(pthread_mutex_t* m)
{
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
  for(i = 0; i < max_vcores(); i++)
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
  c->waiters[vcore_id()] = 1;
  pthread_mutex_unlock(m);

  volatile int* poll = &c->waiters[vcore_id()];
  while(*poll);

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
static void __attribute__((noinline)) internal_function
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
	assert(t);
	t->dont_migrate = TRUE; // won't set this to false later, since he is dying

	uint32_t vcoreid = vcore_id();
	struct preempt_data *vcpd = &__procdata.vcore_preempt_data[vcoreid];

	t->retval = ret;
	mcs_lock_lock(&queue_lock);
	threads_active--;
	TAILQ_REMOVE(&active_queue, t, next);
	mcs_lock_unlock(&queue_lock);

	printf("[P] Pthread id %d is exiting on vcore %d\n", t->id, vcoreid);
	
	/* once we do this, we might miss a notif_pending, so we need to enter vcore
	 * entry later. */
	vcpd->notif_enabled = FALSE;

	/* Change to the transition context (both TLS and stack). */
	extern void** vcore_thread_control_blocks;
	set_tls_desc(vcore_thread_control_blocks[vcoreid], vcoreid);
	assert(current_thread == t);	
	/* After this, make sure you don't use local variables.  Also, make sure the
	 * compiler doesn't use them without telling you (TODO).  We take some space
	 * off the top of the stack since the compiler is going to assume it has a
	 * stack frame setup when it pushes (actually moves) the current_thread onto
	 * the stack before calling __pthread_cleanup(). */
	set_stack_pointer((void*)vcpd->transition_stack - 32);
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
  memset(b->local_sense,0,sizeof(b->local_sense));

  b->sense = 0;
  b->nprocs = b->count = count;
  mcs_lock_init(&b->lock);
  return 0;
}

int pthread_barrier_wait(pthread_barrier_t* b)
{
  int id = vcore_id();
  int ls = b->local_sense[32*id] = 1 - b->local_sense[32*id];

  mcs_lock_lock(&b->lock);
  int count = --b->count;
  mcs_lock_unlock(&b->lock);

  if(count == 0)
  {
    b->count = b->nprocs;
    b->sense = ls;
    return PTHREAD_BARRIER_SERIAL_THREAD;
  }
  else
  {
    while(b->sense != ls);
    return 0;
  }
}

int pthread_barrier_destroy(pthread_barrier_t* b)
{
  return 0;
}
