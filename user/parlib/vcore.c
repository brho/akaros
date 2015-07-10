#include <parlib/arch/arch.h>
#include <stdbool.h>
#include <errno.h>
#include <parlib/vcore.h>
#include <parlib/mcs.h>
#include <sys/param.h>
#include <parlib/parlib.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdio.h>
#include <parlib/event.h>
#include <parlib/uthread.h>
#include <parlib/ucq.h>
#include <ros/arch/membar.h>
#include <parlib/printf-ext.h>

/* starting with 1 since we alloc vcore0's stacks and TLS in vcore_lib_init(). */
static size_t _max_vcores_ever_wanted = 1;
atomic_t nr_new_vcores_wanted;
atomic_t vc_req_being_handled;

__thread struct syscall __vcore_one_sysc = {.flags = (atomic_t)SC_DONE, 0};

/* Per vcore entery function used when reentering at the top of a vcore's stack */
static __thread void (*__vcore_reentry_func)(void) = NULL;

/* TODO: probably don't want to dealloc.  Considering caching */
static void free_transition_tls(int id)
{
	if (get_vcpd_tls_desc(id)) {
		/* Note we briefly have no TLS desc in VCPD.  This is fine so long as
		 * that vcore doesn't get started fresh before we put in a new desc */
		free_tls(get_vcpd_tls_desc(id));
		set_vcpd_tls_desc(id, NULL);
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
	if (!tcb) {
		errno = ENOMEM;
		return -1;
	}
	set_vcpd_tls_desc(id, tcb);
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

/* This gets called in glibc before calling the programs 'main'.  Need to set
 * ourselves up so that thread0 is a uthread, and then register basic signals to
 * go to vcore 0. */
static void vcore_libc_init(void)
{
	register_printf_specifier('r', printf_errstr, printf_errstr_info);
	/* TODO: register for other kevents/signals and whatnot (can probably reuse
	 * the simple ev_q).  Could also do this via explicit functions from the
	 * program. */
}

void __attribute__((constructor)) vcore_lib_init(void)
{
	uintptr_t mmap_block;

	/* Note this is racy, but okay.  The first time through, we are _S.
	 * Also, this is the "lowest" level constructor for now, so we don't need
	 * to call any other init functions after our run_once() call. This may
	 * change in the future. */
	init_once_racy(return);

	/* Need to alloc vcore0's transition stuff here (technically, just the TLS)
	 * so that schedulers can use vcore0's transition TLS before it comes up in
	 * vcore_entry() */
	if(allocate_transition_stack(0) || allocate_transition_tls(0))
		goto vcore_lib_init_fail;

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
	vcore_libc_init();
	return;
vcore_lib_init_fail:
	assert(0);
}

/* Helper functions used to reenter at the top of a vcore's stack for an
 * arbitrary function */
static void __attribute__((noinline, noreturn)) 
__vcore_reenter()
{
  __vcore_reentry_func();
  assert(0);
}

void vcore_reenter(void (*entry_func)(void))
{
  assert(in_vcore_context());
  struct preempt_data *vcpd = vcpd_of(vcore_id());

  __vcore_reentry_func = entry_func;
  set_stack_pointer((void*)vcpd->transition_stack);
  cmb();
  __vcore_reenter();
}

/* Helper, picks some sane defaults and changes the process into an MCP */
void vcore_change_to_m(void)
{
	int ret;
	__procdata.res_req[RES_CORES].amt_wanted = 1;
	__procdata.res_req[RES_CORES].amt_wanted_min = 1;	/* whatever */
	assert(!in_multi_mode());
	assert(!in_vcore_context());
	ret = sys_change_to_m();
	assert(!ret);
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
		sys_poke_ksched(0, RES_CORES);	/* 0 -> poke for ourselves */
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
	unsigned long old_nr;
	uint32_t vcoreid = vcore_id();
	struct preempt_data *vcpd = vcpd_of(vcoreid);
	__sync_fetch_and_and(&vcpd->flags, ~VC_CAN_RCV_MSG);
	/* no wrmb() necessary, handle_events() has an mb() if it is checking */
	/* Clears notif pending and tries to handle events.  This is an optimization
	 * to avoid the yield syscall if we have an event pending.  If there is one,
	 * we want to unwind and return to the 2LS loop, where we may not want to
	 * yield anymore.
	 * Note that the kernel only cares about CAN_RCV_MSG for the desired vcore,
	 * not for a FALLBACK.  */
	if (handle_events(vcoreid)) {
		__sync_fetch_and_or(&vcpd->flags, VC_CAN_RCV_MSG);
		return;
	}
	/* If we are yielding since we don't want the core, tell the kernel we want
	 * one less vcore (vc_yield assumes a dumb 2LS).
	 *
	 * If yield fails (slight race), we may end up having more vcores than
	 * amt_wanted for a while, and might lose one later on (after a
	 * preempt/timeslicing) - the 2LS will have to notice eventually if it
	 * actually needs more vcores (which it already needs to do).  amt_wanted
	 * could even be 0.
	 *
	 * In general, any time userspace decrements or sets to 0, it could get
	 * preempted, so the kernel will still give us at least one, until the last
	 * vcore properly yields without missing a message (and becomes a WAITING
	 * proc, which the ksched will not give cores to).
	 *
	 * I think it's possible for userspace to do this (lock, read amt_wanted,
	 * check all message queues for all vcores, subtract amt_wanted (not set to
	 * 0), unlock) so long as every event handler +1s the amt wanted, but that's
	 * a huge pain, and we already have event handling code making sure a
	 * process can't sleep (transition to WAITING) if a message arrives (can't
	 * yield if notif_pending, can't go WAITING without yielding, and the event
	 * posting the notif_pending will find the online VC or be delayed by
	 * spinlock til the proc is WAITING). */
	if (!preempt_pending) {
		do {
			old_nr = __procdata.res_req[RES_CORES].amt_wanted;
			if (old_nr == 0)
				break;
		} while (!__sync_bool_compare_and_swap(
		             &__procdata.res_req[RES_CORES].amt_wanted,
		             old_nr, old_nr - 1));
	}
	/* We can probably yield.  This may pop back up if notif_pending became set
	 * by the kernel after we cleared it and we lost the race. */
	sys_yield(preempt_pending);
	__sync_fetch_and_or(&vcpd->flags, VC_CAN_RCV_MSG);
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
 * up by an IPI.  For now, this is a halt.  Maybe an mwait in the future.
 *
 * This will return if an event was pending (could be the one you were waiting
 * for) or if the halt failed for some reason, such as a concurrent RKM.  If
 * successful, this will not return at all, and the vcore will restart from the
 * top next time it wakes.  Any sort of IRQ will wake the core.
 *
 * Alternatively, I might make this so it never returns, if that's easier to
 * work with (similar issues with yield). */
void vcore_idle(void)
{
	uint32_t vcoreid = vcore_id();
	/* Once we enable notifs, the calling context will be treated like a uthread
	 * (saved into the uth slot).  We don't want to ever run it again, so we
	 * need to make sure there's no cur_uth. */
	assert(!current_uthread);
	/* This clears notif_pending (check, signal, check again pattern). */
	if (handle_events(vcoreid))
		return;
	/* This enables notifs, but also checks notif pending.  At this point, any
	 * new notifs will restart the vcore from the top. */
	enable_notifs(vcoreid);
	/* From now, til we get into the kernel, any notifs will permanently destroy
	 * this context and start the VC from the top.
	 *
	 * Once we're in the kernel, any messages (__notify, __preempt), will be
	 * RKMs.  halt will need to check for those atomically.  Checking for
	 * notif_pending in the kernel (sleep only if not set) is not enough, since
	 * not all reasons for the kernel to stay awak set notif_pending (e.g.,
	 * __preempts and __death).
	 *
	 * At this point, we're out of VC ctx, so anyone who sets notif_pending
	 * should also send an IPI / __notify */
	sys_halt_core(0);
	/* in case halt returns without actually restarting the VC ctx. */
	disable_notifs(vcoreid);
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
	static __thread unsigned int __vc_relax_spun = 0;
	assert(in_vcore_context());
	if (__vc_relax_spun++ >= NR_RELAX_SPINS) {
		/* if vcoreid == vcore_id(), this might be expensive */
		ensure_vcore_runs(vcoreid);
		__vc_relax_spun = 0;
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
