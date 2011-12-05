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
#include <ucq.h>
#include <ros/arch/membar.h>

/* starting with 1 since we alloc vcore0's stacks and TLS in vcore_init(). */
static size_t _max_vcores_ever_wanted = 1;
atomic_t nr_new_vcores_wanted;
atomic_t vc_req_being_handled;

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

/* Reinitialize / reset / refresh a TLS to its initial values.  This doesn't do
 * it properly yet, it merely frees and re-allocates the TLS, which is why we're
 * slightly ghetto and return the pointer you should use for the TCB. */
void *reinit_tls(void *tcb)
{
	/* TODO: keep this in sync with the methods used in
	 * allocate_transition_tls() */
	free_tls(tcb);
	return allocate_tls();
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
	struct preempt_data *vcpd = vcpd_of(id);
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
	uintptr_t mmap_block;
	/* Note this is racy, but okay.  The only time it'll be 0 is the first time
	 * through, when we are _S */
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

	/* Initialize our VCPD event queues' ucqs, two pages per ucq, 4 per vcore */
	mmap_block = (uintptr_t)mmap(0, PGSIZE * 4 * max_vcores(),
	                             PROT_WRITE | PROT_READ,
	                             MAP_POPULATE | MAP_ANONYMOUS, -1, 0);
	/* Yeah, this doesn't fit in the error-handling scheme, but this whole
	 * system doesn't really handle failure, and needs a rewrite involving less
	 * mmaps/munmaps. */
	assert(mmap_block);
	/* Note we may end up doing vcore 0's elsewhere, for _Ss, or else have a
	 * separate ev_q for that. */
	for (int i = 0; i < max_vcores(); i++) {
		/* four pages total for both ucqs from the big block (2 pages each) */
		ucq_init_raw(&vcpd_of(i)->ev_mbox_public.ev_msgs,
		             mmap_block + (4 * i    ) * PGSIZE,
		             mmap_block + (4 * i + 1) * PGSIZE);
		ucq_init_raw(&vcpd_of(i)->ev_mbox_private.ev_msgs,
		             mmap_block + (4 * i + 2) * PGSIZE,
		             mmap_block + (4 * i + 3) * PGSIZE);
	}
	atomic_init(&vc_req_being_handled, 0);
	assert(!in_vcore_context());
	initialized = 1;
	/* no longer need to enable notifs on vcore 0, it is set like that by
	 * default (so you drop into vcore context immediately on transtioning to
	 * _M) */
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
 * 1), since the kernel won't block the call.
 *
 * There are a few concurrency concerns.  We have _max_vcores_ever_wanted,
 * initialization of new vcore stacks/TLSs, making sure we don't ask for too
 * many (minor point), and most importantly not asking the kernel for too much
 * or otherwise miscommunicating our desires to the kernel.  Remember, the
 * kernel wants just one answer from the process about what it wants, and it is
 * up to the process to figure that out.
 *
 * So we basically have one thread do the submitting/prepping/bookkeeping, and
 * other threads come in just update the number wanted and make sure someone
 * is sorting things out.  This will perform a bit better too, since only one
 * vcore makes syscalls (which hammer the proc_lock).  This essentially has
 * cores submit work, and one core does the work (like Eric's old delta
 * functions).
 *
 * There's a slight semantic change: this will return 0 (success) for the
 * non-submitters, and 0 if we submitted.  -1 only if the submitter had some
 * non-kernel failure.
 *
 * Also, beware that this (like the old version) doesn't protect with races on
 * num_vcores().  num_vcores() is how many you have now or very soon (accounting
 * for messages in flight that will take your cores), not how many you told the
 * kernel you want. */
int vcore_request(long nr_new_vcores)
{
	long nr_to_prep_now, nr_vcores_wanted;

	if (vcore_init() < 0)
		return -1;	/* consider ERRNO */
	/* Early sanity checks */
	if ((nr_new_vcores < 0) || (nr_new_vcores + num_vcores() > max_vcores()))
		return -1;	/* consider ERRNO */
	/* Post our desires (ROS atomic_add() conflicts with glibc) */
	atomic_fetch_and_add(&nr_new_vcores_wanted, nr_new_vcores);
try_handle_it:
	cmb();	/* inc before swap.  the atomic is a CPU mb() */
	if (atomic_swap(&vc_req_being_handled, 1)) {
		/* We got a 1 back, so someone else is already working on it */
		return 0;
	}
handle_it:
	/* So now we're the ones supposed to handle things.  Figure out how many we
	 * have, though this is racy.  Yields/preempts/grants will change this over
	 * time, and we may end up asking for less than we had. */
	nr_vcores_wanted = num_vcores();
	cmb();	/* force a reread of num_vcores() later */
	/* Pull all of the vcores wanted into our local variable, where we'll deal
	 * with prepping/requesting that many vcores.  Keep doing this til we think
	 * no more are wanted. */
	while ((nr_to_prep_now = atomic_swap(&nr_new_vcores_wanted, 0))) {
		nr_vcores_wanted += nr_to_prep_now;
		/* Don't bother prepping or asking for more than we can ever get */
		nr_vcores_wanted = MIN(nr_vcores_wanted, max_vcores());
		/* Make sure all we might ask for are prepped */
		for(long i = _max_vcores_ever_wanted; i < nr_vcores_wanted; i++) {
			if (allocate_transition_stack(i) || allocate_transition_tls(i)) {
				atomic_set(&vc_req_being_handled, 0);	/* unlock and bail out*/
				return -1;
			}
			_max_vcores_ever_wanted++;	/* done in the loop to handle failures*/
		}
	}
	/* Got all the ones we can get, let's submit it to the kernel.  We check
	 * against num_vcores() one last time, though we still have some races... */
	if (nr_vcores_wanted > num_vcores()) {
		sys_resource_req(RES_CORES, nr_vcores_wanted, 1, 0);
		cmb();	/* force a reread of num_vcores() at handle_it: */
		goto handle_it;
	}
	/* Here, we believe there are none left to do */
	atomic_set(&vc_req_being_handled, 0);	/* unlock, to allow others to try */
	/* Double check for any that might have come in while we were out */
	if (atomic_read(&nr_new_vcores_wanted))
		goto try_handle_it;
	return 0;
}

/* This can return, if you failed to yield due to a concurrent event. */
void vcore_yield(bool preempt_pending)
{
	uint32_t vcoreid = vcore_id();
	struct preempt_data *vcpd = vcpd_of(vcoreid);
	vcpd->can_rcv_msg = FALSE;
	/* no wrmb() necessary, clear_notif() has an mb() */
	/* Clears notif pending.  If we had an event outstanding, this will handle
	 * it and return TRUE, at which point we want to unwind and return to the
	 * 2LS loop (where we may not want to yield anymore).  Note that the kernel
	 * only cares about can_rcv_msg for the desired vcore, not for a FALLBACK.
	 * We need to deal with this notif_pending business regardless of
	 * can_rcv_msg.  We just want to avoid a yield syscall if possible.  It is
	 * important that clear_notif_pending will handle_events().  That is
	 * necessary to do/check after setting can_rcv_msg to FALSE. */
	if (clear_notif_pending(vcoreid)) {
		vcpd->can_rcv_msg = TRUE;
		return;
	}
	/* We can probably yield.  This may pop back up if notif_pending became set
	 * by the kernel after we cleared it and we lost the race. */
	sys_yield(preempt_pending);
	vcpd->can_rcv_msg = TRUE;
}

/* Clear pending, and try to handle events that came in between a previous call
 * to handle_events() and the clearing of pending.  While it's not a big deal,
 * we'll loop in case we catch any.  Will break out of this once there are no
 * events, and we will have send pending to 0. 
 *
 * Note that this won't catch every race/case of an incoming event.  Future
 * events will get caught in pop_ros_tf() or proc_yield().
 *
 * Also note that this handles events, which may change your current uthread or
 * might not return!  Be careful calling this.  Check run_uthread for an example
 * of how to use this. */
bool clear_notif_pending(uint32_t vcoreid)
{
	bool handled_event = FALSE;
	do {
		vcpd_of(vcoreid)->notif_pending = 0;
		/* need a full mb(), since handle events might be just a read or might
		 * be a write, either way, it needs to happen after notif_pending */
		mb();
		handled_event = handle_events(vcoreid);
	} while (handled_event);
	return handled_event;
}

/* Enables notifs, and deals with missed notifs by self notifying.  This should
 * be rare, so the syscall overhead isn't a big deal.  The other alternative
 * would be to uthread_yield(), which would require us to revert some uthread
 * interface changes. */
void enable_notifs(uint32_t vcoreid)
{
	__enable_notifs(vcoreid);
	wrmb();	/* need to read after the write that enabled notifs */
	/* Note we could get migrated before executing this.  If that happens, our
	 * vcore had gone into vcore context (which is what we wanted), and this
	 * self_notify to our old vcore is spurious and harmless. */
	if (vcpd_of(vcoreid)->notif_pending)
		sys_self_notify(vcoreid, EV_NONE, 0);
}

/* Helper to disable notifs.  It simply checks to make sure we disabled uthread
 * migration, which is a common mistake. */
void disable_notifs(uint32_t vcoreid)
{
	if (!in_vcore_context() && current_uthread)
		assert(current_uthread->flags & UTHREAD_DONT_MIGRATE);
	__disable_notifs(vcoreid);
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
