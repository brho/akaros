/* Copyright (c) 2018 Google Inc
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * RCU.  We borrow a few things from Linux - mostly the header bits and the
 * tree-rcu structure.
 *
 * Acronyms/definitions:
 * - CB: RCU callbacks (call_rcu)
 * - QS: quiescent state - a time when we know a core isn't in an RCU read-side
 *   critical section.
 * - GP: grace period.  Some quotes from Linux/Paul:
 *   - "A time period during which all such pre-existing readers complete is
 *   called a 'grace period'."
 *   - "Anything outside of an RCU read-side critical section is a quiescent
 *   state, and a grace period is any time period in which every CPU (or task,
 *   for
 * - gpnum: number of the current grace period we are working on
 * - completed: number of the grace periods completed
 *
 * We differ in a few ways from Linux's implementation:
 *
 * - Callbacks run on management cores (a.k.a, LL cores, e.g. core 0).  This way
 *   we don't have to kick idle or user space cores to run their CBs, and those
 *   CBs don't interfere with a possibly unrelated process.
 *
 * - Our RCU is most similar to rcu_sched (classic RCU), and not the preemptible
 *   RCU.  Our kthreads don't get preempted, so we don't need to worry about
 *   read side critical sections being interrupted.
 *
 * - There is no softirq processing to note the passing of GPs or to run CBs.
 *
 * - Our tree uses atomic ops to trace grace periods within the rcu_nodes.
 *   Linux's tree-rcu uses locks.  They need the locks since under some
 *   circumstances, a QS would be marked during a read-side critical section,
 *   and the QS marking needed to track the gpnum to keep the QS matched to the
 *   GP.  See
 *   https://www.kernel.org/doc/Documentation/RCU/Design/Data-Structures/Data-Structures.html
 *   and grep "Come on".  We don't need to worry about this since we only mark a
 *   QS under two situations:
 *
 *   - The core knows it is does not hold an rcu_read_lock, so we can always
 *   mark QS.
 *   - The GP kthread saw the core either idle or in userspace after the gp
 *   started.  That means we know that core had a QS after the GP started.
 *
 *   So any time we mark a QS is actually a QS.  I think Linux has times where
 *   they note a QS for an older GP, and set a note to mark that QS *for that
 *   GP* in the future.  Their locks make sure they are marking for the right
 *   gpnum.  There might be some element of the rnps not knowing about the
 *   latest GP yet too.
 *
 * - We do use locking at the per-core level to decide whether or not to start
 *   mark a QS for a given GP.  (lock, compare gp_acked to gpnum, etc).  This
 *   ensures only one thread (the core or the GP kth) marks the core for a given
 *   GP.  We actually could handle it if the both did, (make the trickle-up
 *   idempotent, which we do for the interior nodes) but we could run into
 *   situations where a core checks in for a GP before the global gpnum was set.
 *   This could happen when the GP kth is resetting the tree for the next GP.
 *   I think it'd be OK, but not worth the hassle and confusion.
 *
 * - We have a kthread for GP management, like Linux.  Callbacks are enqueued
 *   locally (on the core that calls call_rcu), like Linux.  We have a kthread
 *   per management core to process the callbacks, and these threads will handle
 *   the callbacks of *all* cores.  Each core has a specific mgmt kthread that
 *   will run its callbacks.  It is important that a particular core's callbacks
 *   are processed by the same thread - I rely on this to implement rcu_barrier
 *   easily.  In that case, we just need to schedule a CB on every core that has
 *   CBs, and when those N CBs are done, our barrier passed.  This relies on CBs
 *   being processed in order for a given core.  We could do the barrier in
 *   other ways, but it doesn't seem like a big deal.
 *
 * - I kept around some seq counter and locking stuff in rcu_helper.h.  We might
 *   use that in the future.
 */

#include <rcu.h>
#include <kthread.h>
#include <smp.h>
#include <kmalloc.h>

/* How many CBs to queue up before we trigger a GP */
#define RCU_CB_THRESH 10
/* How long (usec) we wait between running a GP if we weren't triggered. */
#define RCU_GP_MIN_PERIOD 25000
/* How long (usec) we wait for cores to check in. */
#define RCU_GP_TARDY_PERIOD 1000

/* In rcu_tree_helper.c */
extern int rcu_num_cores;
extern int rcu_num_lvls;

/* Controls whether we skip cores when we expedite, which forces tardy cores. */
static bool rcu_debug_tardy;

/* Externed in rcu_tree_helper.c */
struct rcu_state rcu_state;


DEFINE_PERCPU(struct rcu_pcpui, rcu_pcpui);

struct sync_cb_blob {
	struct rcu_head h;
	struct semaphore *sem;
};

static void __sync_cb(struct rcu_head *head)
{
	struct sync_cb_blob *b = container_of(head, struct sync_cb_blob, h);

	sem_up(b->sem);
}

void synchronize_rcu(void)
{
	struct sync_cb_blob b[1];
	struct semaphore sem[1];

	if (is_rcu_ktask(current_kthread))
		panic("Attempted synchronize_rcu() from an RCU callback!");
	sem_init(sem, 0);
	init_rcu_head_on_stack(&b->h);
	b->sem = sem;
	call_rcu(&b->h, __sync_cb);
	sem_down(sem);
}

static inline bool gp_in_progress(struct rcu_state *rsp)
{
	unsigned long completed = READ_ONCE(rsp->completed);
	unsigned long gpnum = READ_ONCE(rsp->gpnum);

	assert(gpnum - completed <= 1);
	return completed != gpnum;
}

/* Wakes the kthread to run a grace period if it isn't already running.
 *
 * If 'force', we'll make sure it runs a fresh GP, which will catch all CBs
 * registered before this call.  That's not 100% true.  It might be possible on
 * some non-x86 architectures for the writes that wake the ktask are reordered
 * before the read of gpnum that our caller made.  Thus the caller could have a
 * CB in a later GP.  Worst case, they'll wait an extra GP timeout.  Not too
 * concerned, though I probably should be. */
static void wake_gp_ktask(struct rcu_state *rsp, bool force)
{
	if (!force && gp_in_progress(rsp))
		return;
	rsp->gp_ktask_ctl = 1;
	rendez_wakeup(&rsp->gp_ktask_rv);
}

static void rcu_exec_cb(struct rcu_head *head)
{
	if (__is_kfree_rcu_offset((unsigned long)head->func))
		kfree((void*)head - (unsigned long)head->func);
	else
		head->func(head);
}

static void __early_call_rcu_kmsg(uint32_t srcid, long a0, long a1, long a2)
{
	rcu_exec_cb((struct rcu_head*)a0);
}

void __early_call_rcu(struct rcu_head *head)
{
	extern bool booting;

	assert(booting);
	assert(core_id() == 0);
	send_kernel_message(0, __early_call_rcu_kmsg, (long)head, 0, 0,
	                    KMSG_ROUTINE);
}

/* This could be called from a remote core, e.g. rcu_barrier().  Returns the
 * number of enqueued CBs, including the one we pass in. */
static int __call_rcu_rpi(struct rcu_state *rsp, struct rcu_pcpui *rpi,
                           struct rcu_head *head, rcu_callback_t func)
{
	unsigned int nr_cbs;

	head->func = func;

	if (!rpi->booted) {
		__early_call_rcu(head);
		return 0;
	}
	/* rsp->gpnum is the one we're either working on (if > completed) or the one
	 * we already did.  Either way, it's a GP that may have already been ACKed
	 * during a core's QS, and that core could have started a read-side critical
	 * section that must complete before CB runs.  That requires another GP. */
	head->gpnum = READ_ONCE(rsp->gpnum) + 1;
	spin_lock_irqsave(&rpi->lock);
	list_add_tail(&head->link, &rpi->cbs);
	nr_cbs = ++rpi->nr_cbs;
	spin_unlock_irqsave(&rpi->lock);
	/* rcu_barrier requires that the write to ->nr_cbs be visible before any
	 * future writes.  unlock orders the write inside, but doesn't prevent other
	 * writes from moving in.  Technically, our lock implementations do that,
	 * but it's not part of our definition.  Maybe it should be.  Til then: */
	wmb();
	return nr_cbs;
}

/* Minus the kfree offset check */
static void __call_rcu(struct rcu_head *head, rcu_callback_t func)
{
	struct rcu_pcpui *rpi = PERCPU_VARPTR(rcu_pcpui);
	struct rcu_state *rsp = rpi->rsp;
	unsigned int thresh;

	thresh = __call_rcu_rpi(rsp, rpi, head, func);
	if (thresh > RCU_CB_THRESH)
		wake_gp_ktask(rpi->rsp, false);
}

void call_rcu(struct rcu_head *head, rcu_callback_t func)
{
	assert(!__is_kfree_rcu_offset((unsigned long)func));
	__call_rcu(head, func);
}

void rcu_barrier(void)
{
	struct rcu_state *rsp = PERCPU_VAR(rcu_pcpui).rsp;
	struct rcu_pcpui *rpi;
	struct semaphore sem[1];
	struct sync_cb_blob *b;
	int nr_sent = 0;

	if (is_rcu_ktask(current_kthread))
		panic("Attempted rcu_barrier() from an RCU callback!");
	/* TODO: if we have concurrent rcu_barriers, we might be able to share the
	 * CBs.  Say we have 1 CB on a core, then N rcu_barriers.  We'll have N
	 * call_rcus in flight, though we could share.  Linux does this with a mtx
	 * and some accounting, I think. */

	b = kzmalloc(sizeof(struct sync_cb_blob) * num_cores, MEM_WAIT);
	/* Remember, you block when sem is <= 0.  We'll get nr_sent ups, and we'll
	 * down 1 for each.  This is just like the synchronize_rcu() case; there,
	 * nr_sent == 1. */
	sem_init(sem, 0);
	/* Order any signal we received from someone who called call_rcu() before
	 * our rpi->nr_cbs reads. */
	rmb();
	for_each_core(i) {
		rpi = _PERCPU_VARPTR(rcu_pcpui, i);
		/* Lockless peek at nr_cbs.  Two things to note here:
		 * - We look at nr_cbs and not the list, since there could be CBs on the
		 *   stack-local work list or that have blocked.
		 * - The guarantee is that we wait for any CBs from call_rcus that can
		 *   be proved to happen before rcu_barrier.  That means call_rcu had to
		 *   return, which means it had to set the nr_cbs. */
		if (!rpi->nr_cbs)
			continue;
		init_rcu_head_on_stack(&b[i].h);
		b[i].sem = sem;
		__call_rcu_rpi(rsp, rpi, &b[i].h, __sync_cb);
		nr_sent++;
	}
	if (!nr_sent) {
		kfree(b);
		return;
	}
	wake_gp_ktask(rpi->rsp, true);
	/* sem_down_bulk is currently slow.  Even with some fixes, we actually want
	 * a barrier, which you could imagine doing with a tree.  sem_down_bulk()
	 * doesn't have the info that we have: that the wakeups are coming from N
	 * cores on the leaves of the tree. */
	sem_down_bulk(sem, nr_sent);
	kfree(b);
}

void rcu_force_quiescent_state(void)
{
	/* It's unclear if we want to block until the QS has passed */
	wake_gp_ktask(PERCPU_VAR(rcu_pcpui).rsp, true);
}

void kfree_call_rcu(struct rcu_head *head, rcu_callback_t off)
{
	__call_rcu(head, off);
}

/* Clears the bits core(s) in grpmask present in rnp, trickling up to the root.
 * Note that a 1 in qsmask means you haven't checked in - like a todo list.
 * Last one out kicks the GP kthread. */
static void __mark_qs(struct rcu_state *rsp, struct rcu_node *rnp,
                      unsigned long grpmask)
{
	unsigned long new_qsm;

	new_qsm = __sync_and_and_fetch(&rnp->qsmask, ~grpmask);
	/* I don't fully understand this, but we need some form of transitive
	 * barrier across the entire tree.  Linux does this when they lock/unlock.
	 * Our equivalent is the atomic op. */
	smp_mb__after_unlock_lock();
	/* Only one thread will get 0 back - the last one to check in */
	if (new_qsm)
		return;
	if (rnp->parent)
		__mark_qs(rsp, rnp->parent, rnp->grpmask);
	else
		rendez_wakeup(&rsp->gp_ktask_rv);
}

static void rcu_report_qs_rpi(struct rcu_state *rsp, struct rcu_pcpui *rpi)
{
	/* Note we don't check ->completed == ->gpnum (gp_in_progress()).  We only
	 * care if our core hasn't reported in for a GP.  This time is a subset of
	 * gp_in_progress. */
	if (rpi->gp_acked == READ_ONCE(rsp->gpnum)) {
		/* If a GP starts right afterwards, oh well.  Catch it next time. */
		return;
	}
	/* Lock ensures we only report a QS once per GP. */
	spin_lock_irqsave(&rpi->lock);
	if (rpi->gp_acked == READ_ONCE(rsp->gpnum)) {
		spin_unlock_irqsave(&rpi->lock);
		return;
	}
	/* A gp can start concurrently, but once started, we should never be behind
	 * by more than 1. */
	assert(rpi->gp_acked + 1 == READ_ONCE(rsp->gpnum));
	/* Up our gp_acked before actually marking it.  I don't want to hold the
	 * lock too long (e.g. some debug code in rendez_wakeup() calls call_rcu).
	 * So we've unlocked, but haven't actually checked in yet - that's fine.  No
	 * one else will attempt to check in until the next GP, which can't happen
	 * until after we check in for this GP. */
	rpi->gp_acked++;
	spin_unlock_irqsave(&rpi->lock);
	__mark_qs(rsp, rpi->my_node, rpi->grpmask);
}

/* Cores advertise when they are in QSs.  If the core already reported in, or if
 * we're not in a GP, this is a quick check (given a global read of ->gpnum). */
void rcu_report_qs(void)
{
	rcu_report_qs_rpi(&rcu_state, PERCPU_VARPTR(rcu_pcpui));
}

/* For debugging checks on large trees.  Keep this in sync with
 * rcu_init_fake_cores(). */
static void rcu_report_qs_fake_cores(struct rcu_state *rsp)
{
	struct rcu_node *rnp;

	rnp = rsp->level[rcu_num_lvls - 1];
	for (int i = num_cores; i < rcu_num_cores; i++) {
		while (i > rnp->grphi)
			rnp++;
		if (rcu_debug_tardy && (i % 2))
			continue;
		__mark_qs(rsp, rnp, 1 << (i - rnp->grplo));
	}
}

static void rcu_report_qs_remote_core(struct rcu_state *rsp, int coreid)
{
	int cpu_state = READ_ONCE(pcpui_var(coreid, cpu_state));
	struct rcu_pcpui *rpi = _PERCPU_VARPTR(rcu_pcpui, coreid);

	/* Lockless peek.  If we ever saw them idle/user after a GP started, we
	 * know they had a QS, and we know we're still in the original GP. */
	if (cpu_state == CPU_STATE_IDLE || cpu_state == CPU_STATE_USER)
		rcu_report_qs_rpi(rsp, rpi);
}

/* Checks every core, remotely via the cpu state, to see if it is in a QS.
 * This is like an expedited grace period. */
static void rcu_report_qs_remote_cores(struct rcu_state *rsp)
{
	for_each_core(i) {
		if (rcu_debug_tardy && (i % 2))
			continue;
		rcu_report_qs_remote_core(rsp, i);
	}
}

static void rcu_report_qs_tardy_cores(struct rcu_state *rsp)
{
	struct rcu_node *rnp;
	unsigned long qsmask;
	int i;

	rcu_for_each_leaf_node(rsp, rnp) {
		qsmask = READ_ONCE(rnp->qsmask);
		if (!qsmask)
			continue;
		for_each_set_bit(i, &qsmask, BITS_PER_LONG) {
			/* Fake cores */
			if (i + rnp->grplo >= num_cores) {
				__mark_qs(rsp, rnp, 1 << i);
				continue;
			}
			rcu_report_qs_remote_core(rsp, i + rnp->grplo);
		}
	}
}

static int root_qsmask_empty(void *arg)
{
	struct rcu_state *rsp = arg;

	return READ_ONCE(rsp->node[0].qsmask) == 0 ? 1 : 0;
}

static void rcu_run_gp(struct rcu_state *rsp)
{
	struct rcu_node *rnp;

	assert(rsp->gpnum == rsp->completed);
	/* Initialize the tree for accumulating QSs.  We know there are no users on
	 * the tree.  The only time a core looks at the tree is when reporting a QS
	 * for a GP.  The previous GP is done, thus all cores reported their GP
	 * already (for the previous GP), and they won't try again until we
	 * advertise the next GP. */
	rcu_for_each_node_breadth_first(rsp, rnp)
		rnp->qsmask = rnp->qsmaskinit;
	/* Need the tree set for reporting QSs before advertising the GP */
	wmb();
	WRITE_ONCE(rsp->gpnum, rsp->gpnum + 1);
	/* At this point, the cores can start reporting in. */
	/* Fake cores help test a tree larger than num_cores. */
	rcu_report_qs_fake_cores(rsp);
	/* Expediting aggressively.  We could also wait briefly and then check the
	 * tardy cores. */
	rcu_report_qs_remote_cores(rsp);
	/* Note that even when we expedite the GP by checking remote cores, there's
	 * a race where a core halted but we didn't see it.  (they report QS, decide
	 * to halt, pause, we start GP, see they haven't halted, etc.  They could
	 * report the QS after setting the state, but I didn't want to . */
	do {
		rendez_sleep_timeout(&rsp->gp_ktask_rv, root_qsmask_empty, rsp,
		                     RCU_GP_TARDY_PERIOD);
		rcu_report_qs_tardy_cores(rsp);
	} while (!root_qsmask_empty(rsp));
	/* Not sure if we need any barriers here.  Once we post 'completed', the CBs
	 * can start running.  But no one should touch the tree til gpnum is
	 * incremented. */
	WRITE_ONCE(rsp->completed, rsp->gpnum);
}

static int should_wake_ctl(void *arg)
{
	int *ctl = arg;

	return *ctl != 0 ? 1 : 0;
}

static void wake_mgmt_ktasks(struct rcu_state *rsp)
{
	struct rcu_pcpui *rpi;

	/* TODO: For each mgmt core */
	rpi = _PERCPU_VARPTR(rcu_pcpui, 0);
	rpi->mgmt_ktask_ctl = 1;
	rendez_wakeup(&rpi->mgmt_ktask_rv);
}

static void rcu_gp_ktask(void *arg)
{
	struct rcu_state *rsp = arg;

	current_kthread->flags |= KTH_IS_RCU_KTASK;
	while (1) {
		rendez_sleep_timeout(&rsp->gp_ktask_rv, should_wake_ctl,
		                     &rsp->gp_ktask_ctl, RCU_GP_MIN_PERIOD);
		rsp->gp_ktask_ctl = 0;
		/* Our write of 0 must happen before starting the GP.  If rcu_barrier's
		 * CBs miss the start of the GP (and thus are in an unscheduled GP),
		 * their write of 1 must happen after our write of 0 so that we rerun.
		 * This is the post-and-poke pattern.  It's not a huge deal, since we'll
		 * catch it after the GP period timeout. */
		wmb();
		rcu_run_gp(rsp);
		wake_mgmt_ktasks(rsp);
	};
}

static void run_rcu_cbs(struct rcu_state *rsp, int coreid)
{
	struct rcu_pcpui *rpi = _PERCPU_VARPTR(rcu_pcpui, coreid);
	struct list_head work = LIST_HEAD_INIT(work);
	struct rcu_head *head, *temp, *last_for_gp = NULL;
	int nr_cbs = 0;
	unsigned long completed;

	/* We'll run the CBs for any GP completed so far, but not any GP that could
	 * be completed concurrently.  "CBs for a GP" means callbacks that must wait
	 * for that GP to complete. */
	completed = READ_ONCE(rsp->completed);

	/* This lockless peek is an optimization.  We're guaranteed to not miss the
	 * CB for the given GP: If the core had a CB for this GP, it must have
	 * put it on the list before checking in, before the GP completes, and
	 * before we run. */
	if (list_empty(&rpi->cbs))
		return;

	spin_lock_irqsave(&rpi->lock);
	list_for_each_entry(head, &rpi->cbs, link) {
		if (ULONG_CMP_LT(completed, head->gpnum))
			break;
		nr_cbs++;
		last_for_gp = head;
	}
	if (last_for_gp)
		list_cut_position(&work, &rpi->cbs, &last_for_gp->link);
	spin_unlock_irqsave(&rpi->lock);

	if (!nr_cbs) {
		assert(list_empty(&work));
		return;
	}
	list_for_each_entry_safe(head, temp, &work, link) {
		list_del(&head->link);
		rcu_exec_cb(head);
	}

	/* We kept nr_cbs in place until the CBs, which could block, completed.
	 * This allows other readers (rcu_barrier()) of our pcpui to tell if we have
	 * any CBs pending.  This relies on us being the only consumer/runner of CBs
	 * for this core. */
	spin_lock_irqsave(&rpi->lock);
	rpi->nr_cbs -= nr_cbs;
	spin_unlock_irqsave(&rpi->lock);
}

static void rcu_mgmt_ktask(void *arg)
{
	struct rcu_pcpui *rpi = arg;
	struct rcu_state *rsp = rpi->rsp;

	current_kthread->flags |= KTH_IS_RCU_KTASK;
	while (1) {
		rendez_sleep(&rpi->mgmt_ktask_rv, should_wake_ctl,
		             &rpi->mgmt_ktask_ctl);
		rpi->mgmt_ktask_ctl = 0;
		/* TODO: given the number of mgmt kthreads, we need to assign cores */
		for_each_core(i)
			run_rcu_cbs(rsp, i);
	};
}

void rcu_init_pcpui(struct rcu_state *rsp, struct rcu_pcpui *rpi, int coreid)
{
	struct rcu_node *rnp = rpi->my_node;

	rpi->rsp = rsp;
	assert(rnp->grplo <= coreid);
	assert(coreid <= rnp->grphi);
	rpi->coreid = coreid;
	rpi->grpnum = coreid - rnp->grplo;
	rpi->grpmask = 1 << rpi->grpnum;
	rpi->booted = false;

	/* We're single threaded now, so this is OK. */
	rnp->qsmaskinit |= rpi->grpmask;

	spinlock_init_irqsave(&rpi->lock);
	INIT_LIST_HEAD(&rpi->cbs);
	rpi->nr_cbs = 0;
	rpi->gp_acked = rsp->completed;

	/* TODO: For each mgmt core only */
	if (coreid == 0) {
		rendez_init(&rpi->mgmt_ktask_rv);
		rpi->mgmt_ktask_ctl = 0;
	}
}

/* Initializes the fake cores.  Works with rcu_report_qs_fake_cores() */
static void rcu_init_fake_cores(struct rcu_state *rsp)
{
	struct rcu_node *rnp;

	rnp = rsp->level[rcu_num_lvls - 1];
	for (int i = num_cores; i < rcu_num_cores; i++) {
		while (i > rnp->grphi)
			rnp++;
		rnp->qsmaskinit |= 1 << (i - rnp->grplo);
	}
}

void rcu_init(void)
{
	struct rcu_state *rsp = &rcu_state;
	struct rcu_pcpui *rpi;

	rcu_init_geometry();
	rcu_init_one(rsp);
	rcu_init_fake_cores(rsp);
	rcu_dump_rcu_node_tree(rsp);

	ktask("rcu_gp", rcu_gp_ktask, rsp);
	/* TODO: For each mgmt core */
	ktask("rcu_mgmt_0", rcu_mgmt_ktask, _PERCPU_VARPTR(rcu_pcpui, 0));

	/* If we have a call_rcu before percpu_init, we might be using the spot in
	 * the actual __percpu .section.  We'd be core 0, so that'd be OK, since all
	 * we're using it for is reading 'booted'. */
	for_each_core(i) {
		rpi = _PERCPU_VARPTR(rcu_pcpui, i);
		rpi->booted = true;
	}
}
