#include <arch/arch.h>
#include <stdbool.h>
#include <errno.h>
#include <vcore.h>
#include <mcs.h>
#include <sys/param.h>
#include <parlib.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <rstdio.h>
#include <glibc-tls.h>
#include <event.h>
#include <ros/arch/membar.h>

/* starting with 1 since we alloc vcore0's stacks and TLS in vcore_init(). */
static size_t _max_vcores_ever_wanted = 1;
static mcs_lock_t _vcore_lock = MCS_LOCK_INIT;

/* Which operations we'll call for the 2LS.  Will change a bit with Lithe.  For
 * now, there are no defaults.  2LSs can override sched_ops. */
struct schedule_ops default_2ls_ops = {0};
struct schedule_ops *sched_ops __attribute__((weak)) = &default_2ls_ops;

extern void** vcore_thread_control_blocks;

__thread struct uthread *current_thread = 0;

/* Get a TLS, returns 0 on failure.  Vcores have their own TLS, and any thread
 * created by a user-level scheduler needs to create a TLS as well. */
void *allocate_tls(void)
{
	extern void *_dl_allocate_tls(void *mem) internal_function;
	void *tcb = _dl_allocate_tls(NULL);
	if (!tcb)
		return 0;
	/* Make sure the TLS is set up properly - its tcb pointer points to itself.
	 * Keep this in sync with sysdeps/ros/XXX/tls.h.  For whatever reason,
	 * dynamically linked programs do not need this to be redone, but statics
	 * do. */
	tcbhead_t *head = (tcbhead_t*)tcb;
	head->tcb = tcb;
	head->self = tcb;
	return tcb;
}

/* TODO: probably don't want to dealloc.  Considering caching */
static void free_transition_tls(int id)
{
	extern void _dl_deallocate_tls (void *tcb, bool dealloc_tcb) internal_function;
	if(vcore_thread_control_blocks[id])
	{
		_dl_deallocate_tls(vcore_thread_control_blocks[id],true);
		vcore_thread_control_blocks[id] = NULL;
	}
}

static int allocate_transition_tls(int id)
{
	/* We want to free and then reallocate the tls rather than simply 
	 * reinitializing it because its size may have changed.  TODO: not sure if
	 * this is right.  0-ing is one thing, but freeing and reallocating can be
	 * expensive, esp if syscalls are involved.  Check out glibc's
	 * allocatestack.c for what might work. */
	free_transition_tls(id);

	void *tcb = allocate_tls();

	if ((vcore_thread_control_blocks[id] = tcb) == NULL) {
		errno = ENOMEM;
		return -1;
	}
	return 0;
}

static void free_transition_stack(int id)
{
	// don't actually free stacks
}

static int allocate_transition_stack(int id)
{
	struct preempt_data *vcpd = &__procdata.vcore_preempt_data[id];
	if (vcpd->transition_stack)
		return 0; // reuse old stack

	void* stackbot = mmap(0, TRANSITION_STACK_SIZE,
	                      PROT_READ|PROT_WRITE|PROT_EXEC,
	                      MAP_POPULATE|MAP_ANONYMOUS, -1, 0);

	if(stackbot == MAP_FAILED)
		return -1; // errno set by mmap

	vcpd->transition_stack = (uintptr_t)stackbot + TRANSITION_STACK_SIZE;

	return 0;
}

int vcore_init()
{
	static int initialized = 0;
	if(initialized)
		return 0;

	vcore_thread_control_blocks = (void**)calloc(max_vcores(),sizeof(void*));

	if(!vcore_thread_control_blocks)
		goto vcore_init_fail;

	/* Need to alloc vcore0's transition stuff here (technically, just the TLS)
	 * so that schedulers can use vcore0's transition TLS before it comes up in
	 * vcore_entry() */
	if(allocate_transition_stack(0) || allocate_transition_tls(0))
		goto vcore_init_tls_fail;

	assert(!in_vcore_context());

	/* Bug if vcore init was called with no 2LS */
	assert(sched_ops->sched_init);
	/* Get thread 0's thread struct (2LS allocs it) */
	struct uthread *uthread = sched_ops->sched_init();
	
	/* Save a pointer to thread0's tls region (the glibc one) into its tcb */
	uthread->tls_desc = get_tls_desc(0);
	/* Save a pointer to the uthread in its own TLS */
	current_thread = uthread;

	/* Change temporarily to vcore0s tls region so we can save the newly created
	 * tcb into its current_thread variable and then restore it.  One minor
	 * issue is that vcore0's transition-TLS isn't TLS_INITed yet.  Until it is
	 * (right before vcore_entry(), don't try and take the address of any of
	 * its TLS vars. */
	extern void** vcore_thread_control_blocks;
	set_tls_desc(vcore_thread_control_blocks[0], 0);
	current_thread = uthread;
	set_tls_desc(uthread->tls_desc, 0);
	assert(!in_vcore_context());

	/* don't forget to enable notifs on vcore0.  if you don't, the kernel will
	 * restart your _S with notifs disabled, which is a path to confusion. */
	enable_notifs(0);

	initialized = 1;
	return 0;

vcore_init_tls_fail:
	free(vcore_thread_control_blocks);
vcore_init_fail:
	errno = ENOMEM;
	return -1;
}

/* Returns -1 with errno set on error, or 0 on success.  This does not return
 * the number of cores actually granted (though some parts of the kernel do
 * internally).
 *
 * Note the doesn't block or anything (despite the min number requested is
 * 1), since the kernel won't block the call. */
int vcore_request(size_t k)
{
	int ret = -1;
	size_t i,j;

	if(vcore_init() < 0)
		return -1;

	// TODO: could do this function without a lock once we 
	// have atomic fetch and add in user space
	mcs_lock_lock(&_vcore_lock);

	size_t vcores_wanted = num_vcores() + k;
	if(k < 0 || vcores_wanted > max_vcores())
	{
		errno = EAGAIN;
		goto fail;
	}

	for(i = _max_vcores_ever_wanted; i < vcores_wanted; i++)
	{
		if(allocate_transition_stack(i) || allocate_transition_tls(i))
			goto fail; // errno set by the call that failed
		_max_vcores_ever_wanted++;
	}
	ret = sys_resource_req(RES_CORES, vcores_wanted, 1, 0);

fail:
	mcs_lock_unlock(&_vcore_lock);
	return ret;
}

void vcore_yield()
{
	sys_yield(0);
}

/* Deals with a pending preemption (checks, responds).  If the 2LS registered a
 * function, it will get run.  Returns true if you got preempted.  Called
 * 'check' instead of 'handle', since this isn't an event handler.  It's the "Oh
 * shit a preempt is on its way ASAP". */
bool check_preempt_pending(uint32_t vcoreid)
{
	bool retval = FALSE;
	if (__procinfo.vcoremap[vcoreid].preempt_pending) {
		retval = TRUE;
		if (sched_ops->preempt_pending)
			sched_ops->preempt_pending();
		/* this tries to yield, but will pop back up if this was a spurious
		 * preempt_pending. */
		sys_yield(TRUE);
	}
	return retval;
}

/* Clear pending, and try to handle events that came in between a previous call
 * to handle_events() and the clearing of pending.  While it's not a big deal,
 * we'll loop in case we catch any.  Will break out of this once there are no
 * events, and we will have send pending to 0. 
 *
 * Note that this won't catch every race/case of an incoming event.  Future
 * events will get caught in pop_ros_tf() */
void clear_notif_pending(uint32_t vcoreid)
{
	do {
		cmb();
		__procdata.vcore_preempt_data[vcoreid].notif_pending = 0;
	} while (handle_events(vcoreid));
}

/****************** uthread *******************/
/* static helpers: */
static int __uthread_allocate_tls(struct uthread *uthread);
static void __uthread_free_tls(struct uthread *uthread);

/* 2LSs shouldn't call vcore_entry() directly */
// XXX this is going to break testing apps like mhello and syscall
void __attribute__((noreturn)) vcore_entry()
{
	uint32_t vcoreid = vcore_id();

	struct preempt_data *vcpd = &__procdata.vcore_preempt_data[vcoreid];

	/* Should always have notifications disabled when coming in here. */
	assert(vcpd->notif_enabled == FALSE);
	assert(in_vcore_context());

	check_preempt_pending(vcoreid);
	handle_events(vcoreid);
	assert(in_vcore_context());	/* double check, in case and event changed it */
	assert(sched_ops->sched_entry);
	sched_ops->sched_entry();
	/* If we get here, the 2LS entry returned.  We can call out to the 2LS for
	 * guidance about whether or not to yield, etc.  Or the 2LS can do it and
	 * just not return.  Whatever we do, it ought to parallel what we do for
	 * requesting more cores in uthread_create(). */
	printd("Vcore %d is yielding\n", vcoreid);
	sys_yield(0);
	assert(0);
}

/* Could move this, along with start_routine and arg, into the 2LSs */
static void __uthread_run(void)
{
	struct uthread *me = current_thread;
	uthread_exit(me->start_routine(me->arg));
}

/* Creates a uthread.  Will pass udata to sched_ops's thread_create.  For now,
 * the vcore/default 2ls code handles start routines and args.  Mostly because
 * this is used when initing a utf, which is vcore specific for now. */
struct uthread *uthread_create(void *(*start_routine)(void *), void *arg,
                               void *udata)
{
	/* First time through, init the vcore code (which makes a uthread out of
	 * thread0 / the current code.  Could move this to a ctor. */
	static bool first = TRUE;
	if (first) {
		if (vcore_init())		/* could make this uthread_init */
			printf("Vcore init failed!\n");
		first = FALSE;
	}
	assert(!in_vcore_context());
	assert(sched_ops->thread_create);
	struct uthread *new_thread = sched_ops->thread_create(udata);
	assert(new_thread->stacktop);
	new_thread->start_routine = start_routine;
	new_thread->arg = arg;
	/* Set the u_tf to start up in __pthread_run, which will call the real
	 * start_routine and pass it the arg. */
	init_user_tf(&new_thread->utf, (uint32_t)__uthread_run, 
                 (uint32_t)(new_thread->stacktop));
	/* Get a TLS */
	assert(!__uthread_allocate_tls(new_thread));
	/* Switch into the new guys TLS and let it know who it is */
	struct uthread *caller = current_thread;
	assert(caller);
	/* Don't migrate this thread to another vcore, since it depends on being on
	 * the same vcore throughout. */
	caller->dont_migrate = TRUE;
	wmb();
	/* Note the first time we call this, we technically aren't on a vcore */
	uint32_t vcoreid = vcore_id();
	/* Save the new_thread to the new uthread in that uthread's TLS */
	set_tls_desc(new_thread->tls_desc, vcoreid);
	current_thread = new_thread;
	/* Switch back to the caller */
	set_tls_desc(caller->tls_desc, vcoreid);
	/* Okay to migrate now. */
	wmb();
	caller->dont_migrate = FALSE;
	/* Allow the 2LS to make the thread runnable, and do whatever. */
	assert(sched_ops->thread_runnable);
	sched_ops->thread_runnable(new_thread);
	/* This is where we'll call out to a smarter 2LS function to see if we want
	 * to get more cores (and how many). */
	/* Need to get some vcores.  If this is the first time, we'd like to get
	 * two: one for the main thread (aka thread0), and another for the pthread
	 * we are creating.  Can rework this if we get another vcore interface that
	 * deals with absolute core counts.
	 *
	 * Need to get at least one core to put us in _M mode so we can run the 2LS,
	 * etc, so for now we'll just spin until we get at least one (might be none
	 * available).
	 *
	 * TODO: do something smarter regarding asking for cores (paired with
	 * yielding), and block or something until one core is available (will need
	 * kernel support). */
	static bool first_time = TRUE;
	if (first_time) {
		first_time = FALSE;
		/* Try for two, don't settle for less than 1 */
		while (num_vcores() < 1) {
			vcore_request(2);
			cpu_relax();
		}
	} else {	/* common case */
		/* Try to get another for the new thread, but doesn't matter if we get
		 * one or not, so long as we still have at least 1. */
		vcore_request(1);
	}
	return new_thread;
}

/* Need to have this as a separate, non-inlined function since we clobber the
 * stack pointer before calling it, and don't want the compiler to play games
 * with my hart.
 *
 * TODO: combine this 2-step logic with uthread_exit() */
static void __attribute__((noinline, noreturn)) 
__uthread_yield(struct uthread *uthread)
{
	assert(in_vcore_context());
	/* TODO: want to set this to FALSE once we no longer depend on being on this
	 * vcore.  Though if we are using TLS, we are depending on the vcore.  Since
	 * notifs are disabled and we are in a transition context, we probably
	 * shouldn't be moved anyway.  It does mean that a pthread could get jammed.
	 * If we do this after putting it on the active list, we'll have a race on
	 * dont_migrate. */
	uthread->dont_migrate = FALSE;
	assert(sched_ops->thread_yield);
	/* 2LS will save the thread somewhere for restarting.  Later on, we'll
	 * probably have a generic function for all sorts of waiting. */
	sched_ops->thread_yield(uthread);
	/* Leave the current vcore completely */
	current_thread = NULL; // this might be okay, even with a migration
	/* Go back to the entry point, where we can handle notifications or
	 * reschedule someone. */
	vcore_entry();
}

/* Calling thread yields.  TODO: combine similar code with uthread_exit() (done
 * like this to ease the transition to the 2LS-ops */
void uthread_yield(void)
{
	struct uthread *uthread = current_thread;
	volatile bool yielding = TRUE; /* signal to short circuit when restarting */
	/* TODO: (HSS) Save silly state */
	// save_fp_state(&t->as);
	assert(!in_vcore_context());
	/* Don't migrate this thread to another vcore, since it depends on being on
	 * the same vcore throughout (once it disables notifs). */
	uthread->dont_migrate = TRUE;
	wmb();
	uint32_t vcoreid = vcore_id();
	printd("[U] Uthread %08p is yielding on vcore %d\n", uthread, vcoreid);
	struct preempt_data *vcpd = &__procdata.vcore_preempt_data[vcoreid];
	/* once we do this, we might miss a notif_pending, so we need to enter vcore
	 * entry later.  Need to disable notifs so we don't get in weird loops with
	 * save_ros_tf() and pop_ros_tf(). */
	disable_notifs(vcoreid);
	/* take the current state and save it into t->utf when this pthread
	 * restarts, it will continue from right after this, see yielding is false,
	 * and short ciruit the function. */
	save_ros_tf(&uthread->utf);
	if (!yielding)
		goto yield_return_path;
	yielding = FALSE; /* for when it starts back up */
	/* Change to the transition context (both TLS and stack). */
	extern void** vcore_thread_control_blocks;
	set_tls_desc(vcore_thread_control_blocks[vcoreid], vcoreid);
	assert(current_thread == uthread);	
	assert(in_vcore_context());	/* technically, we aren't fully in vcore context */
	/* After this, make sure you don't use local variables.  Note the warning in
	 * pthread_exit() */
	set_stack_pointer((void*)vcpd->transition_stack);
	/* Finish exiting in another function. */
	__uthread_yield(current_thread);
	/* Should never get here */
	assert(0);
	/* Will jump here when the pthread's trapframe is restarted/popped. */
yield_return_path:
	printd("[U] Uthread %08p returning from a yield!\n", uthread);
}

/* Need to have this as a separate, non-inlined function since we clobber the
 * stack pointer before calling it, and don't want the compiler to play games
 * with my hart. */
static void __attribute__((noinline, noreturn)) 
__uthread_exit(struct uthread *uthread)
{
	assert(in_vcore_context());
	/* we alloc and manage the TLS, so lets get rid of it */
	__uthread_free_tls(uthread);
	/* 2LS specific cleanup */
	assert(sched_ops->thread_exit);
	sched_ops->thread_exit(uthread);
	current_thread = NULL;
	/* Go back to the entry point, where we can handle notifications or
	 * reschedule someone. */
	vcore_entry();
}

/* Exits from the uthread */
void uthread_exit(void *retval)
{
	assert(!in_vcore_context());
	struct uthread *uthread = current_thread;
	uthread->retval = retval;
	/* Don't migrate this thread to anothe vcore, since it depends on being on
	 * the same vcore throughout. */
	uthread->dont_migrate = TRUE; // won't set to false later, since he is dying
	wmb();
	uint32_t vcoreid = vcore_id();
	struct preempt_data *vcpd = &__procdata.vcore_preempt_data[vcoreid];
	printd("[U] Uthread %08p is exiting on vcore %d\n", uthread, vcoreid);
	/* once we do this, we might miss a notif_pending, so we need to enter vcore
	 * entry later. */
	disable_notifs(vcoreid);
	/* Change to the transition context (both TLS and stack). */
	extern void** vcore_thread_control_blocks;
	set_tls_desc(vcore_thread_control_blocks[vcoreid], vcoreid);
	assert(current_thread == uthread);	
	/* After this, make sure you don't use local variables.  Also, make sure the
	 * compiler doesn't use them without telling you (TODO).
	 *
	 * In each arch's set_stack_pointer, make sure you subtract off as much room
	 * as you need to any local vars that might be pushed before calling the
	 * next function, or for whatever other reason the compiler/hardware might
	 * walk up the stack a bit when calling a noreturn function. */
	set_stack_pointer((void*)vcpd->transition_stack);
	/* Finish exiting in another function.  Ugh. */
	__uthread_exit(current_thread);
}

/* Runs whatever thread is vcore's current_thread */
void run_current_uthread(void)
{
	uint32_t vcoreid = vcore_id();
	struct preempt_data *vcpd = &__procdata.vcore_preempt_data[vcoreid];
	assert(current_thread);
	printd("[U] Vcore %d is restarting uthread %d\n", vcoreid, uthread->id);
	clear_notif_pending(vcoreid);
	set_tls_desc(current_thread->tls_desc, vcoreid);
	/* Pop the user trap frame */
	pop_ros_tf(&vcpd->notif_tf, vcoreid);
	assert(0);
}

/* Launches the uthread on the vcore */
void run_uthread(struct uthread *uthread)
{
	/* Save a ptr to the pthread running in the transition context's TLS */
	uint32_t vcoreid = vcore_id();
	struct preempt_data *vcpd = &__procdata.vcore_preempt_data[vcoreid];
	printd("[U] Vcore %d is starting uthread %d\n", vcoreid, uthread->id);
	current_thread = uthread;
	clear_notif_pending(vcoreid);
	set_tls_desc(uthread->tls_desc, vcoreid);
	/* Load silly state (Floating point) too.  For real */
	/* TODO: (HSS) */
	/* Pop the user trap frame */
	pop_ros_tf(&uthread->utf, vcoreid);
	assert(0);
}

/* TLS helpers */
static int __uthread_allocate_tls(struct uthread *uthread)
{
	assert(!uthread->tls_desc);
	uthread->tls_desc = allocate_tls();
	if (!uthread->tls_desc) {
		errno = ENOMEM;
		return -1;
	}
	return 0;
}

/* TODO: probably don't want to dealloc.  Considering caching */
static void __uthread_free_tls(struct uthread *uthread)
{
	extern void _dl_deallocate_tls (void *tcb, bool dealloc_tcb) internal_function;

	assert(uthread->tls_desc);
	_dl_deallocate_tls(uthread->tls_desc, TRUE);
	uthread->tls_desc = NULL;
}
