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
#include <event.h>
#include <uthread.h>
#include <ucq.h>
#include <ros/arch/membar.h>

/* starting with 1 since we alloc vcore0's stacks and TLS in vcore_init(). */
static size_t _max_vcores_ever_wanted = 1;
atomic_t nr_new_vcores_wanted;
atomic_t vc_req_being_handled;

extern void** vcore_thread_control_blocks;

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

/* this, plus tricking gcc into thinking this is -u (undefined), AND including
 * the event_init in it, causes the linker to need to check parlib.a and see the
 * strong symbol... */
void force_parlib_symbols(void)
{
	vcore_event_init();
	assert(0);
}

/* This gets called in glibc before calling the programs 'main'.  Need to set
 * ourselves up so that thread0 is a uthread, and then register basic signals to
 * go to vcore 0. */
void vcore_event_init(void)
{
	/* set up our thread0 as a uthread */
	uthread_slim_init();
	/* TODO: register for other kevents/signals and whatnot (can probably reuse
	 * the simple ev_q).  Could also do this via explicit functions from the
	 * program. */
}

/* Helper, picks some sane defaults and changes the process into an MCP */
void vcore_change_to_m(void)
{
	__procdata.res_req[RES_CORES].amt_wanted = 1;
	__procdata.res_req[RES_CORES].amt_wanted_min = 1;	/* whatever */
	assert(!in_multi_mode());
	assert(!in_vcore_context());
	assert(!sys_change_to_m());
	assert(in_multi_mode());
	assert(!in_vcore_context());
}

/* Returns -1 with errno set on error, or 0 on success.  This does not return
 * the number of cores actually granted (though some parts of the kernel do
 * internally).
 *
 * This tries to get "more vcores", based on the number we currently have.
 * We'll probably need smarter 2LSs in the future that just directly set
 * amt_wanted.  What happens is we can have a bunch of 2LS vcore contexts
 * trying to get "another vcore", which currently means more than num_vcores().
 * If you have someone ask for two more, and then someone else ask for one more,
 * how many you ultimately ask for depends on if the kernel heard you and
 * adjusted num_vcores in between the two calls.  Or maybe your amt_wanted
 * already was num_vcores + 5, so neither call is telling the kernel anything
 * new.  It comes down to "one more than I have" vs "one more than I've already
 * asked for".
 *
 * So for now, this will keep the older behavior (one more than I have).  It
 * will try to accumulate any concurrent requests, and adjust amt_wanted up.
 * Interleaving, repetitive calls (everyone asking for one more) may get
 * ignored.
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
	/* So now we're the ones supposed to handle things.  This does things in the
	 * "increment based on the number we have", vs "increment on the number we
	 * said we want".
	 *
	 * Figure out how many we have, though this is racy.  Yields/preempts/grants
	 * will change this over time, and we may end up asking for less than we
	 * had. */
	nr_vcores_wanted = num_vcores();
	/* Pull all of the vcores wanted into our local variable, where we'll deal
	 * with prepping/requesting that many vcores.  Keep doing this til we think
	 * no more are wanted. */
	while ((nr_to_prep_now = atomic_swap(&nr_new_vcores_wanted, 0))) {
		nr_vcores_wanted += nr_to_prep_now;
		/* Don't bother prepping or asking for more than we can ever get */
		nr_vcores_wanted = MIN(nr_vcores_wanted, max_vcores());
		/* Make sure all we might ask for are prepped */
		for (long i = _max_vcores_ever_wanted; i < nr_vcores_wanted; i++) {
			if (allocate_transition_stack(i) || allocate_transition_tls(i)) {
				atomic_set(&vc_req_being_handled, 0);	/* unlock and bail out*/
				return -1;
			}
			_max_vcores_ever_wanted++;	/* done in the loop to handle failures*/
		}
	}
	cmb();	/* force a reread of num_vcores() */
	/* Update amt_wanted if we now want *more* than what the kernel already
	 * knows.  See notes in the func doc. */
	if (nr_vcores_wanted > __procdata.res_req[RES_CORES].amt_wanted)
		__procdata.res_req[RES_CORES].amt_wanted = nr_vcores_wanted;
	/* If num_vcores isn't what we want, we can poke the ksched.  Due to some
	 * races with yield, our desires may be old.  Not a big deal; any vcores
	 * that pop up will just end up yielding (or get preempt messages.)  */
	if (nr_vcores_wanted > num_vcores())
		sys_poke_ksched(RES_CORES);
	/* Unlock, (which lets someone else work), and check to see if more work
	 * needs to be done.  If so, we'll make sure it gets handled. */
	atomic_set(&vc_req_being_handled, 0);	/* unlock, to allow others to try */
	wrmb();
	/* check for any that might have come in while we were out */
	if (atomic_read(&nr_new_vcores_wanted))
		goto try_handle_it;
	return 0;
}

/* This can return, if you failed to yield due to a concurrent event.  Note
 * we're atomicly setting the CAN_RCV flag, and aren't bothering with CASing
 * (either with the kernel or uthread's handle_indirs()).  We don't particularly
 * care what other code does - we intend to set those flags no matter what. */
void vcore_yield(bool preempt_pending)
{
	uint32_t vcoreid = vcore_id();
	struct preempt_data *vcpd = vcpd_of(vcoreid);
	__sync_fetch_and_and(&vcpd->flags, ~VC_CAN_RCV_MSG);
	/* no wrmb() necessary, clear_notif() has an mb() */
	/* Clears notif pending.  If we had an event outstanding, this will handle
	 * it and return TRUE, at which point we want to unwind and return to the
	 * 2LS loop (where we may not want to yield anymore).  Note that the kernel
	 * only cares about CAN_RCV_MSG for the desired vcore, not for a FALLBACK.
	 * We need to deal with this notif_pending business regardless of
	 * CAN_RCV_MSG.  We just want to avoid a yield syscall if possible.  It is
	 * important that clear_notif_pending will handle_events().  That is
	 * necessary to do/check after turning off CAN_RCV_MSG. */
	if (clear_notif_pending(vcoreid)) {
		__sync_fetch_and_or(&vcpd->flags, VC_CAN_RCV_MSG);
		return;
	}
	/* If we are yielding since we don't want the core, tell the kernel we want
	 * one less vcore.  If yield fails (slight race), we may end up having more
	 * vcores than amt_wanted for a while, and might lose one later on (after a
	 * preempt/timeslicing) - the 2LS will have to notice eventually if it
	 * actually needs more vcores (which it already needs to do).  We need to
	 * atomically decrement, though I don't want the kernel's data type here to
	 * be atomic_t (only userspace cares in this one case). */
	if (!preempt_pending)
		__sync_fetch_and_sub(&__procdata.res_req[RES_CORES].amt_wanted, 1);
	/* We can probably yield.  This may pop back up if notif_pending became set
	 * by the kernel after we cleared it and we lost the race. */
	sys_yield(preempt_pending);
	__sync_fetch_and_or(&vcpd->flags, VC_CAN_RCV_MSG);
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
		sys_self_notify(vcoreid, EV_NONE, 0, TRUE);
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

/* Helper, that actually makes sure a vcore is running.  Call this is you really
 * want vcoreid.  More often, you'll want to call the regular version. */
static void __ensure_vcore_runs(uint32_t vcoreid)
{
	if (vcore_is_preempted(vcoreid)) {
		printd("[vcore]: VC %d changing to VC %d\n", vcore_id(), vcoreid);
		/* Note that at this moment, the vcore could still be mapped (we're
		 * racing with __preempt.  If that happens, we'll just fail the
		 * sys_change_vcore(), and next time __ensure runs we'll get it. */
		/* We want to recover them from preemption.  Since we know they have
		 * notifs disabled, they will need to be directly restarted, so we can
		 * skip the other logic and cut straight to the sys_change_vcore() */
		sys_change_vcore(vcoreid, FALSE);
	}
}

/* Helper, looks for any preempted vcores, making sure each of them runs at some
 * point.  This is pretty heavy-weight, and should be used to help get out of
 * weird deadlocks (spinning in vcore context, waiting on another vcore).  If
 * you might know which vcore you are waiting on, use ensure_vc_runs. */
static void __ensure_all_run(void)
{
	for (int i = 0; i < max_vcores(); i++)
		__ensure_vcore_runs(i);
}

/* Makes sure a vcore is running.  If it is preempted, we'll switch to
 * it.  This will return, either immediately if the vcore is running, or later
 * when someone preempt-recovers us.
 *
 * If you pass in your own vcoreid, this will make sure all other preempted
 * vcores run. */
void ensure_vcore_runs(uint32_t vcoreid)
{
	/* if the vcoreid is ourselves, make sure everyone else is running */
	if (vcoreid == vcore_id()) {
		__ensure_all_run();
		return;
	}
	__ensure_vcore_runs(vcoreid);
}

#define NR_RELAX_SPINS 1000
/* If you are spinning in vcore context and it is likely that you don't know who
 * you are waiting on, call this.  It will spin for a bit before firing up the
 * potentially expensive __ensure_all_run().  Don't call this from uthread
 * context.  sys_change_vcore will probably mess you up. */
void cpu_relax_vc(uint32_t vcoreid)
{
	static __thread unsigned int spun;		/* vcore TLS */
	assert(in_vcore_context());
	spun = 0;
	if (spun++ >= NR_RELAX_SPINS) {
		/* if vcoreid == vcore_id(), this might be expensive */
		ensure_vcore_runs(vcoreid);
		spun = 0;
	}
	cpu_relax();
}

/* Check with the kernel to determine what vcore we are.  Normally, you should
 * never call this, since your vcoreid is stored in your TLS.  Also, if you call
 * it from a uthread, you could get migrated, so you should drop into some form
 * of vcore context (DONT_MIGRATE on) */
uint32_t get_vcoreid(void)
{
	if (!in_vcore_context()) {
		assert(current_uthread);
		assert(current_uthread->flags & UTHREAD_DONT_MIGRATE);
	}
	return __get_vcoreid();
}

/* Debugging helper.  Pass in the string you want printed if your vcoreid is
 * wrong, and pass in what vcoreid you think you are.  Don't call from uthread
 * context unless migrations are disabled.  Will print some stuff and return
 * FALSE if you were wrong. */
bool check_vcoreid(const char *str, uint32_t vcoreid)
{
	uint32_t kvcoreid = get_vcoreid();
	if (vcoreid != kvcoreid) {
		ros_debug("%s: VC %d thought it was VC %d\n", str, kvcoreid, vcoreid);
		return FALSE;
	}
	return TRUE;
}
