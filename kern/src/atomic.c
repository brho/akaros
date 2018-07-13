#include <arch/arch.h>
#include <arch/kdebug.h>

#include <bitmask.h>
#include <atomic.h>
#include <error.h>
#include <string.h>
#include <assert.h>
#include <hashtable.h>
#include <smp.h>
#include <kmalloc.h>
#include <kdebug.h>

static void increase_lock_depth(uint32_t coreid)
{
	per_cpu_info[coreid].lock_depth++;
}

static void decrease_lock_depth(uint32_t coreid)
{
	per_cpu_info[coreid].lock_depth--;
}

#ifdef CONFIG_SPINLOCK_DEBUG

/* Put locks you want to ignore here. */
static uintptr_t blacklist_locks[] = {
	//0xffffffffc03bd000,
};

/* Could do this on the output side, though noisly locks will crowd us out */
static bool can_trace(spinlock_t *lock)
{
	for (int i = 0; i < ARRAY_SIZE(blacklist_locks); i++) {
		if (blacklist_locks[i] == (uintptr_t)lock)
			return FALSE;
	}
	return TRUE;
}

/* spinlock and trylock call this after locking */
static void post_lock(spinlock_t *lock, uint32_t coreid)
{
	struct per_cpu_info *pcpui = &per_cpu_info[coreid];
	if ((pcpui->__lock_checking_enabled == 1) && can_trace(lock))
		pcpui_trace_locks(pcpui, lock);
	lock->call_site = get_caller_pc();
	lock->calling_core = coreid;
	/* TODO consider merging this with __ctx_depth (unused field) */
	increase_lock_depth(lock->calling_core);
}

void spin_lock(spinlock_t *lock)
{
	uint32_t coreid = core_id_early();
	struct per_cpu_info *pcpui = &per_cpu_info[coreid];
	/* Short circuit our lock checking, so we can print or do other things to
	 * announce the failure that require locks.  Also avoids anything else
	 * requiring pcpui initialization. */
	if (pcpui->__lock_checking_enabled != 1)
		goto lock;
	if (lock->irq_okay) {
		if (!can_spinwait_irq(pcpui)) {
			pcpui->__lock_checking_enabled--;
			print_kctx_depths("IRQOK");
			panic("Lock %p tried to spin when it shouldn't\n", lock);
			pcpui->__lock_checking_enabled++;
		}
	} else {
		if (!can_spinwait_noirq(pcpui)) {
			pcpui->__lock_checking_enabled--;
			print_kctx_depths("NOIRQ");
			panic("Lock %p tried to spin when it shouldn't\n", lock);
			pcpui->__lock_checking_enabled++;
		}
	}
lock:
	__spin_lock(lock);
	/* Memory barriers are handled by the particular arches */
	post_lock(lock, coreid);
}

/* Trylock doesn't check for irq/noirq, in case we want to try and lock a
 * non-irqsave lock from irq context. */
bool spin_trylock(spinlock_t *lock)
{
	uint32_t coreid = core_id_early();
	bool ret = __spin_trylock(lock);
	if (ret)
		post_lock(lock, coreid);
	return ret;
}

void spin_unlock(spinlock_t *lock)
{
	decrease_lock_depth(lock->calling_core);
	/* Memory barriers are handled by the particular arches */
	assert(spin_locked(lock));
	__spin_unlock(lock);
}

void spinlock_debug(spinlock_t *lock)
{
	uintptr_t pc = lock->call_site;

	if (!pc) {
		printk("Lock %p: never locked\n", lock);
		return;
	}
	printk("Lock %p: currently %slocked.  Last locked at [<%p>] in %s on core %d\n",
	       lock, spin_locked(lock) ? "" : "un", pc, get_fn_name(pc),
	       lock->calling_core);
}

#endif /* CONFIG_SPINLOCK_DEBUG */

/* Inits a hashlock. */
void hashlock_init(struct hashlock *hl, unsigned int nr_entries)
{
	hl->nr_entries = nr_entries;
	/* this is the right way to do it, though memset is faster.  If we ever
	 * find that this is taking a lot of time, we can change it. */
	for (int i = 0; i < hl->nr_entries; i++) {
		spinlock_init(&hl->locks[i]);
	}
}

void hashlock_init_irqsave(struct hashlock *hl, unsigned int nr_entries)
{
	hl->nr_entries = nr_entries;
	/* this is the right way to do it, though memset is faster.  If we ever
	 * find that this is taking a lot of time, we can change it. */
	for (int i = 0; i < hl->nr_entries; i++) {
		spinlock_init_irqsave(&hl->locks[i]);
	}
}

/* Helper, gets the specific spinlock for a hl/key combo. */
static spinlock_t *get_spinlock(struct hashlock *hl, long key)
{
	/* using the hashtable's generic hash function */
	return &hl->locks[__generic_hash((void*)key) % hl->nr_entries];
}

void hash_lock(struct hashlock *hl, long key)
{
	spin_lock(get_spinlock(hl, key));
}

void hash_unlock(struct hashlock *hl, long key)
{
	spin_unlock(get_spinlock(hl, key));
}

void hash_lock_irqsave(struct hashlock *hl, long key)
{
	spin_lock_irqsave(get_spinlock(hl, key));
}

void hash_unlock_irqsave(struct hashlock *hl, long key)
{
	spin_unlock_irqsave(get_spinlock(hl, key));
}

/* This is the 'post (work) and poke' style of sync.  We make sure the poke
 * tracker's function runs.  Once this returns, the func either has run or is
 * currently running (in case someone else is running now).  We won't wait or
 * spin or anything, and it is safe to call this recursively (deeper in the
 * call-graph).
 *
 * It's up to the caller to somehow post its work.  We'll also pass arg to the
 * func, ONLY IF the caller is the one to execute it - so there's no guarantee
 * the func(specific_arg) combo will actually run.  It's more for info
 * purposes/optimizations/etc.  If no one uses it, I'll get rid of it. */
void poke(struct poke_tracker *tracker, void *arg)
{
	atomic_set(&tracker->need_to_run, TRUE);
	/* will need to repeatedly do it if someone keeps posting work */
	do {
		/* want an wrmb() btw posting work/need_to_run and in_progress.  the
		 * swap provides the HW mb. just need a cmb, which we do in the loop to
		 * cover the iterations (even though i can't imagine the compiler
		 * reordering the check it needed to do for the branch).. */
		cmb();
		/* poke / make sure someone does it.  if we get a TRUE (1) back, someone
		 * is already running and will deal with the posted work.  (probably on
		 * their next loop).  if we got a 0 back, we won the race and have the
		 * 'lock'. */
		if (atomic_swap(&tracker->run_in_progress, TRUE))
			return;
		/* if we're here, then we're the one who needs to run the func. */
		/* clear the 'need to run', since we're running it now.  new users will
		 * set it again.  this write needs to be wmb()'d after in_progress.  the
		 * swap provided the HW mb(). */
		cmb();
		atomic_set(&tracker->need_to_run, FALSE);	/* no internal HW mb */
		/* run the actual function.  the poke sync makes sure only one caller is
		 * in that func at a time. */
		assert(tracker->func);
		tracker->func(arg);
		wmb();	/* ensure the in_prog write comes after the run_again. */
		atomic_set(&tracker->run_in_progress, FALSE);	/* no internal HW mb */
		/* in_prog write must come before run_again read */
		wrmb();
	} while (atomic_read(&tracker->need_to_run));	/* while there's more work*/
}

// Must be called in a pair with waiton_checklist
int commit_checklist_wait(checklist_t* list, checklist_mask_t* mask)
{
	assert(list->mask.size == mask->size);
	// abort if the list is locked.  this will protect us from trying to commit
	// and thus spin on a checklist that we are already waiting on.  it is
	// still possible to not get the lock, but the holder is on another core.
	// Or, bail out if we can see the list is already in use.  This check is
	// just an optimization before we try to use the list for real.
	if ((checklist_is_locked(list)) || !checklist_is_clear(list))
		return -EBUSY;

	// possession of this lock means you can wait on it and set it
	spin_lock_irqsave(&list->lock);
	// wait til the list is available.  could have some adaptive thing here
	// where it fails after X tries (like 500), gives up the lock, and returns
	// an error code
	while (!checklist_is_clear(list))
		cpu_relax();

	// list is ours and clear, set it to the settings of our list
	COPY_BITMASK(list->mask.bits, mask->bits, mask->size);
	return 0;
}

int commit_checklist_nowait(checklist_t* list, checklist_mask_t* mask)
{
	int e = 0;
	if ((e = commit_checklist_wait(list, mask)))
		return e;
	// give up the lock, since we won't wait for completion
	spin_unlock_irqsave(&list->lock);
	return e;
}
// The deal with the lock:
// what if two different actors are waiting on the list, but for different reasons?
// part of the problem is we are doing both set and check via the same path
//
// aside: we made this a lot more difficult than the usual barriers or even
// the RCU grace-period checkers, since we have to worry about this construct
// being used by others before we are done with it.
//
// how about this: if we want to wait on this later, we just don't release the
// lock.  if we release it, then we don't care who comes in and grabs and starts
// checking the list.
// 	- regardless, there are going to be issues with people looking for a free
// 	item.  even if they grab the lock, they may end up waiting a while and
// 	wantint to bail (like test for a while, give up, move on, etc).
// 	- still limited in that only the setter can check, and only one person
// 	can spinwait / check for completion.  if someone else tries to wait (wanting
// 	completion), they may miss it if someone else comes in and grabs the lock
// 	to use it for a new checklist
// 		- if we had the ability to sleep and get woken up, we could have a
// 		queue.  actually, we could do a queue anyway, but they all spin
// 		and it's the bosses responsibility to *wake* them

// Must be called after commit_checklist
// Assumed we held the lock if we ever call this
int waiton_checklist(checklist_t* list)
{
	extern atomic_t outstanding_calls;
	// can consider breakout out early, like above, and erroring out
	while (!checklist_is_clear(list))
		cpu_relax();
	spin_unlock_irqsave(&list->lock);
	// global counter of wrappers either waited on or being contended for.
	atomic_dec(&outstanding_calls);
	return 0;
}

// like waiton, but don't bother waiting either
int release_checklist(checklist_t* list)
{
	spin_unlock_irqsave(&list->lock);
	return 0;
}

// peaks in and sees if the list is locked with it's spinlock
int checklist_is_locked(checklist_t* list)
{
	return spin_locked(&list->lock);
}

// no synch guarantees - just looks at the list
int checklist_is_clear(checklist_t* list)
{
	return BITMASK_IS_CLEAR(list->mask.bits, list->mask.size);
}

// no synch guarantees - just looks at the list
int checklist_is_full(checklist_t* list)
{
	return BITMASK_IS_FULL(list->mask.bits, list->mask.size);
}

// no synch guarantees - just resets the list to empty
void reset_checklist(checklist_t* list)
{
	CLR_BITMASK(list->mask.bits, list->mask.size);
}

// CPU mask specific - this is how cores report in
void down_checklist(checklist_t* list)
{
	CLR_BITMASK_BIT_ATOMIC(list->mask.bits, core_id());
}

/* Barriers */
void init_barrier(barrier_t* barrier, uint32_t count)
{
	spinlock_init_irqsave(&barrier->lock);
	barrier->init_count = count;
	barrier->current_count = count;
	barrier->ready = 0;
}

void reset_barrier(barrier_t* barrier)
{
	barrier->current_count = barrier->init_count;
}

// primitive barrier function.  all cores call this.
void waiton_barrier(barrier_t* barrier)
{
	uint8_t local_ready = barrier->ready;

	spin_lock_irqsave(&barrier->lock);
	barrier->current_count--;
	if (barrier->current_count) {
		spin_unlock_irqsave(&barrier->lock);
		while (barrier->ready == local_ready)
			cpu_relax();
	} else {
		spin_unlock_irqsave(&barrier->lock);
		reset_barrier(barrier);
		wmb();
		barrier->ready++;
	}
}
