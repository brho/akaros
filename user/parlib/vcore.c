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
#include <stdio.h>
#include <glibc-tls.h>
#include <event.h>
#include <uthread.h>
#include <ros/arch/membar.h>

/* starting with 1 since we alloc vcore0's stacks and TLS in vcore_init(). */
static size_t _max_vcores_ever_wanted = 1;
static mcs_lock_t _vcore_lock = MCS_LOCK_INIT;

extern void** vcore_thread_control_blocks;

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

/* Free a previously allocated TLS region */
void free_tls(void *tcb)
{
	extern void _dl_deallocate_tls (void *tcb, bool dealloc_tcb) internal_function;
	assert(tcb);
	_dl_deallocate_tls(tcb, TRUE);
}

/* TODO: probably don't want to dealloc.  Considering caching */
static void free_transition_tls(int id)
{
	if(vcore_thread_control_blocks[id])
	{
		free_tls(vcore_thread_control_blocks[id]);
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
	struct mcs_lock_qnode local_qn = {0};
	int ret = -1;
	size_t i,j;

	if(vcore_init() < 0)
		return -1;

	// TODO: could do this function without a lock once we 
	// have atomic fetch and add in user space
	mcs_lock_notifsafe(&_vcore_lock, &local_qn);

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
	/* Ugly hack, but we need to be able to transition to _M mode */
	if (num_vcores() == 0)
		__enable_notifs(vcore_id());
	ret = sys_resource_req(RES_CORES, vcores_wanted, 1, 0);

fail:
	mcs_unlock_notifsafe(&_vcore_lock, &local_qn);
	return ret;
}

void vcore_yield()
{
	sys_yield(0);
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

/* Enables notifs, and deals with missed notifs by self notifying.  This should
 * be rare, so the syscall overhead isn't a big deal. */
void enable_notifs(uint32_t vcoreid)
{
	__enable_notifs(vcoreid);
	if (__procdata.vcore_preempt_data[vcoreid].notif_pending)
		sys_self_notify(vcoreid, EV_NONE, 0);
}

/* Like smp_idle(), this will put the core in a state that it can only be woken
 * up by an IPI.  In the future, we may halt or something. */
void __attribute__((noreturn)) vcore_idle(void)
{
	uint32_t vcoreid = vcore_id();
	clear_notif_pending(vcoreid);
	enable_notifs(vcoreid);
	while (1) {
		cpu_relax();
	}
}
