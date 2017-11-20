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
#include <parlib/poke.h>
#include <parlib/assert.h>
#include <parlib/stdio.h>

__thread int __vcoreid = 0;
__thread bool __vcore_context = FALSE;

__thread struct syscall __vcore_one_sysc = {.flags = (atomic_t)SC_DONE, 0};

/* Per vcore entery function used when reentering at the top of a vcore's stack */
static __thread void (*__vcore_reentry_func)(void) = NULL;

/* The default user vcore_entry function. */
void __attribute__((noreturn)) __vcore_entry(void)
{
	extern void uthread_vcore_entry(void);
	uthread_vcore_entry();
	fprintf(stderr, "vcore_entry() should never return!\n");
	abort();
	__builtin_unreachable();
}
void vcore_entry(void) __attribute__((weak, alias ("__vcore_entry")));

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
	/* Libc function to initialize TLS-based locale info for ctype functions. */
	extern void __ctype_init(void);

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

	/* Setup some intitial TLS data for the newly allocated transition tls. */
	void *temp_tcb = get_tls_desc();
	set_tls_desc(tcb);
	begin_safe_access_tls_vars();
	__vcoreid = id;
	__vcore_context = TRUE;
	__ctype_init();
	end_safe_access_tls_vars();
	set_tls_desc(temp_tcb);

	/* Install the new tls into the vcpd. */
	set_vcpd_tls_desc(id, tcb);
	return 0;
}

static void free_vcore_stack(int id)
{
	// don't actually free stacks
}

static int allocate_vcore_stack(int id)
{
	struct preempt_data *vcpd = vcpd_of(id);
	if (vcpd->vcore_stack)
		return 0; // reuse old stack

	void* stackbot = mmap(0, TRANSITION_STACK_SIZE,
	                      PROT_READ | PROT_WRITE | PROT_EXEC,
	                      MAP_POPULATE | MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);

	if(stackbot == MAP_FAILED)
		return -1; // errno set by mmap

	vcpd->vcore_stack = (uintptr_t)stackbot + TRANSITION_STACK_SIZE;

	return 0;
}

/* Helper: prepares a vcore for use.  Takes a block of pages for the UCQs.
 *
 * Vcores need certain things, such as a stack and TLS.  These are determined by
 * userspace.  Every vcore needs these set up before we drop into vcore context
 * on that vcore.  This means we need to prep before asking the kernel for those
 * vcores.
 *
 * We could have this function do its own mmap, at the expense of O(n) syscalls
 * when we prepare the extra vcores. */
static void __prep_vcore(int vcoreid, uintptr_t mmap_block)
{
	struct preempt_data *vcpd = vcpd_of(vcoreid);
	int ret;

	ret = allocate_vcore_stack(vcoreid);
		assert(!ret);
	ret = allocate_transition_tls(vcoreid);
		assert(!ret);

	vcpd->ev_mbox_public.type = EV_MBOX_UCQ;
	ucq_init_raw(&vcpd->ev_mbox_public.ucq,
	             mmap_block + 0 * PGSIZE,
	             mmap_block + 1 * PGSIZE);
	vcpd->ev_mbox_private.type = EV_MBOX_UCQ;
	ucq_init_raw(&vcpd->ev_mbox_private.ucq,
	             mmap_block + 2 * PGSIZE,
	             mmap_block + 3 * PGSIZE);

	/* Set the lowest level entry point for each vcore. */
	vcpd->vcore_entry = (uintptr_t)__kernel_vcore_entry;
}

static void prep_vcore_0(void)
{
	uintptr_t mmap_block;

	mmap_block = (uintptr_t)mmap(0, PGSIZE * 4,
	                             PROT_WRITE | PROT_READ,
	                             MAP_POPULATE | MAP_ANONYMOUS | MAP_PRIVATE,
	                             -1, 0);
	assert((void*)mmap_block != MAP_FAILED);
	__prep_vcore(0, mmap_block);
}

static void prep_remaining_vcores(void)
{
	uintptr_t mmap_block;

	mmap_block = (uintptr_t)mmap(0, PGSIZE * 4 * (max_vcores() - 1),
	                             PROT_WRITE | PROT_READ,
	                             MAP_POPULATE | MAP_ANONYMOUS | MAP_PRIVATE,
	                             -1, 0);
	assert((void*)mmap_block != MAP_FAILED);
	for (int i = 1; i < max_vcores(); i++)
		__prep_vcore(i, mmap_block + 4 * (i - 1) * PGSIZE);
}

/* Run libc specific early setup code. */
static void vcore_libc_init(void)
{
	register_printf_specifier('r', printf_errstr, printf_errstr_info);
	/* TODO: register for other kevents/signals and whatnot (can probably reuse
	 * the simple ev_q).  Could also do this via explicit functions from the
	 * program. */
}

/* We need to separate the guts of vcore_lib_ctor() into a separate function,
 * since the uthread ctor depends on this ctor running first.
 *
 * Also note that if you make a global ctor (not static, like this used to be),
 * any shared objects that you load when the binary is built with -rdynamic will
 * run the global ctor from the binary, not the one from the .so. */
void vcore_lib_init(void)
{
	/* Note this is racy, but okay.  The first time through, we are _S.
	 * Also, this is the "lowest" level constructor for now, so we don't need
	 * to call any other init functions after our run_once() call. This may
	 * change in the future. */
	parlib_init_once_racy(return);
	/* Need to alloc vcore0's transition stuff here (technically, just the TLS)
	 * so that schedulers can use vcore0's transition TLS before it comes up in
	 * vcore_entry() */
	prep_vcore_0();
	assert(!in_vcore_context());
	vcore_libc_init();
}

static void __attribute__((constructor)) vcore_lib_ctor(void)
{
	if (__in_fake_parlib())
		return;
	vcore_lib_init();
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
  set_stack_pointer((void*)vcpd->vcore_stack);
  cmb();
  __vcore_reenter();
}

/* Helper, picks some sane defaults and changes the process into an MCP */
void vcore_change_to_m(void)
{
	int ret;

	prep_remaining_vcores();
	__procdata.res_req[RES_CORES].amt_wanted = 1;
	__procdata.res_req[RES_CORES].amt_wanted_min = 1;	/* whatever */
	assert(!in_multi_mode());
	assert(!in_vcore_context());
	ret = sys_change_to_m();
	assert(!ret);
	assert(in_multi_mode());
	assert(!in_vcore_context());
}

static void __vc_req_poke(void *nr_vc_wanted)
{
	long nr_vcores_wanted = *(long*)nr_vc_wanted;

	/* We init'd up to max_vcores() VCs during init.  This assumes the kernel
	 * doesn't magically change that value (which it should not do). */
	nr_vcores_wanted = MIN(nr_vcores_wanted, max_vcores());
	if (nr_vcores_wanted > __procdata.res_req[RES_CORES].amt_wanted)
		__procdata.res_req[RES_CORES].amt_wanted = nr_vcores_wanted;
	if (nr_vcores_wanted > num_vcores())
		sys_poke_ksched(0, RES_CORES);	/* 0 -> poke for ourselves */
}
static struct poke_tracker vc_req_poke = POKE_INITIALIZER(__vc_req_poke);

/* Requests the kernel that we have a total of nr_vcores_wanted.
 *
 * This is callable by multiple threads/vcores concurrently.  Exactly one of
 * them will actually run __vc_req_poke.  The others will just return.
 *
 * This means that two threads could ask for differing amounts, and only one of
 * them will succeed.  This is no different than a racy write to a shared
 * variable.  The poke provides a single-threaded environment, so that we don't
 * worry about racing on VCPDs or hitting the kernel with excessive SYS_pokes.
 *
 * Since we're using the post-and-poke style, we can do a 'last write wins'
 * policy for the value used in the poke (and subsequent pokes). */
void vcore_request_total(long nr_vcores_wanted)
{
	static long nr_vc_wanted;

	if (parlib_never_vc_request || !parlib_wants_to_be_mcp)
		return;
	if (nr_vcores_wanted == __procdata.res_req[RES_CORES].amt_wanted)
		return;

	/* We race to "post our work" here.  Whoever handles the poke will get the
	 * latest value written here. */
	nr_vc_wanted = nr_vcores_wanted;
	poke(&vc_req_poke, &nr_vc_wanted);
}

/* This tries to get "more vcores", based on the number we currently have.
 *
 * What happens is we can have a bunch of threads trying to get "another vcore",
 * which currently means more than num_vcores().  If you have someone ask for
 * two more, and then someone else ask for one more, how many you ultimately ask
 * for depends on if the kernel heard you and adjusted num_vcores in between the
 * two calls.  Or maybe your amt_wanted already was num_vcores + 5, so neither
 * call is telling the kernel anything new.  It comes down to "one more than I
 * have" vs "one more than I've already asked for".
 *
 * So for now, this will keep the older behavior (one more than I have).  This
 * is all quite racy, so we can just guess and request a total number of vcores.
 */
void vcore_request_more(long nr_new_vcores)
{
	vcore_request_total(nr_new_vcores + num_vcores());
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

	if (!preempt_pending && parlib_never_yield)
		return;
	__sync_fetch_and_and(&vcpd->flags, ~VC_CAN_RCV_MSG);
	/* no wrmb() necessary, handle_events() has an mb() if it is checking */
	/* Clears notif pending and tries to handle events.  This is an optimization
	 * to avoid the yield syscall if we have an event pending.  If there is one,
	 * we want to unwind and return to the 2LS loop, where we may not want to
	 * yield anymore.
	 * Note that the kernel only cares about CAN_RCV_MSG for the desired vcore;
	 * when spamming, it relies on membership of lists within the kernel.  Look
	 * at spam_list_member() for more info (k/s/event.c). */
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
/* If you are spinning and waiting on another vcore, call this.  Pass in the
 * vcoreid of the core you are waiting on, or your own vcoreid if you don't
 * know.  It will spin for a bit before firing up the potentially expensive
 * __ensure_all_run(). */
void cpu_relax_vc(uint32_t other_vcoreid)
{
	static __thread unsigned int __vc_relax_spun = 0;

	/* Uthreads with notifs enabled can just spin normally.  This actually
	 * depends on the 2LS preemption policy.  Currently, we receive notifs
	 * whenever another core is preempted, so we don't need to poll. */
	if (notif_is_enabled(vcore_id())) {
		cpu_relax();
		return;
	}
	if (__vc_relax_spun++ >= NR_RELAX_SPINS) {
		/* if other_vcoreid == vcore_id(), this might be expensive */
		ensure_vcore_runs(other_vcoreid);
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
		printf("%s: VC %d thought it was VC %d\n", str, kvcoreid, vcoreid);
		return FALSE;
	}
	return TRUE;
}

/* Helper.  Yields the vcore, or restarts it from scratch. */
void __attribute__((noreturn)) vcore_yield_or_restart(void)
{
	struct preempt_data *vcpd = vcpd_of(vcore_id());

	vcore_yield(FALSE);
	/* If vcore_yield returns, we have an event.  Just restart vcore context. */
	set_stack_pointer((void*)vcpd->vcore_stack);
	vcore_entry();
}

void vcore_wake(uint32_t vcoreid, bool force_ipi)
{
	struct preempt_data *vcpd = vcpd_of(vcoreid);

	vcpd->notif_pending = true;
	if (vcoreid == vcore_id())
		return;
	if (force_ipi || !arch_has_mwait())
		sys_self_notify(vcoreid, EV_NONE, 0, true);
}
