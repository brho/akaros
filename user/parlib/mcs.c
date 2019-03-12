#include <parlib/vcore.h>
#include <parlib/mcs.h>
#include <parlib/arch/atomic.h>
#include <string.h>
#include <stdlib.h>
#include <parlib/uthread.h>
#include <parlib/parlib.h>
#include <malloc.h>

// MCS locks
void mcs_lock_init(struct mcs_lock *lock)
{
	memset(lock,0,sizeof(mcs_lock_t));
}

static inline mcs_lock_qnode_t *mcs_qnode_swap(mcs_lock_qnode_t **addr,
                                               mcs_lock_qnode_t *val)
{
	return (mcs_lock_qnode_t*)atomic_swap_ptr((void**)addr, val);
}

void mcs_lock_lock(struct mcs_lock *lock, struct mcs_lock_qnode *qnode)
{
	qnode->next = 0;
	cmb();	/* swap provides a CPU mb() */
	mcs_lock_qnode_t *predecessor = mcs_qnode_swap(&lock->lock, qnode);
	if (predecessor) {
		qnode->locked = 1;
		wmb();
		predecessor->next = qnode;
		/* no need for a wrmb(), since this will only get unlocked after
		 * they read our previous write */
		while (qnode->locked)
			cpu_relax();
	}
	cmb();	/* just need a cmb, the swap handles the CPU wmb/wrmb() */
}

void mcs_lock_unlock(struct mcs_lock *lock, struct mcs_lock_qnode *qnode)
{
	/* Check if someone is already waiting on us to unlock */
	if (qnode->next == 0) {
		cmb(); /* no need for CPU mbs, since there's an atomic_swap() */
		/* Unlock it */
		mcs_lock_qnode_t *old_tail = mcs_qnode_swap(&lock->lock,0);
		/* no one else was already waiting, so we successfully unlocked
		 * and can return */
		if (old_tail == qnode)
			return;
		/* someone else was already waiting on the lock (last one on the
		 * list), and we accidentally took them off.  Try and put it
		 * back. */
		mcs_lock_qnode_t *usurper = mcs_qnode_swap(&lock->lock,old_tail);
		/* since someone else was waiting, they should have made
		 * themselves our next.  spin (very briefly!) til it happens. */
		while (qnode->next == 0)
			cpu_relax();
		if (usurper) {
			/* an usurper is someone who snuck in before we could
			 * put the old tail back.  They now have the lock.
			 * Let's put whoever is supposed to be next as their
			 * next one. */
			usurper->next = qnode->next;
		} else {
			/* No usurper meant we put things back correctly, so we
			 * should just pass the lock / unlock whoever is next */
			qnode->next->locked = 0;
		}
	} else {
		/* mb()s necessary since we didn't call an atomic_swap() */
		/* need to make sure any previous writes don't pass unlocking */
		wmb();
		/* need to make sure any reads happen before the unlocking */
		rwmb();
		/* simply unlock whoever is next */
		qnode->next->locked = 0;
	}
}

/* CAS style mcs locks, kept around til we use them.  We're using the
 * usurper-style, since RISCV doesn't have a real CAS (yet?). */
void mcs_lock_unlock_cas(struct mcs_lock *lock, struct mcs_lock_qnode *qnode)
{
	/* Check if someone is already waiting on us to unlock */
	if (qnode->next == 0) {
		cmb();	/* no need for CPU mbs, since there's an atomic_cas() */
		/* If we're still the lock, just swap it with 0 (unlock) and
		 * return */
		if (atomic_cas_ptr((void**)&lock->lock, qnode, 0))
			return;
		/* We failed, someone is there and we are some (maybe a
		 * different) thread's pred.  Since someone else was waiting,
		 * they should have made themselves our next.  Spin (very
		 * briefly!) til it happens. */
		while (qnode->next == 0)
			cpu_relax();
		/* Alpha wants a read_barrier_depends() here */
		/* Now that we have a next, unlock them */
		qnode->next->locked = 0;
	} else {
		/* mb()s necessary since we didn't call an atomic_swap() */
		/* need to make sure any previous writes don't pass unlocking */
		wmb();
		/* need to make sure any reads happen before the unlocking */
		rwmb();
		/* simply unlock whoever is next */
		qnode->next->locked = 0;
	}
}

/* We don't bother saving the state, like we do with irqsave, since we can use
 * whether or not we are in vcore context to determine that.  This means you
 * shouldn't call this from those moments when you fake being in vcore context
 * (when switching into the TLS, etc). */
void mcs_lock_notifsafe(struct mcs_lock *lock, struct mcs_lock_qnode *qnode)
{
	uth_disable_notifs();
	mcs_lock_lock(lock, qnode);
}

/* Note we turn off the DONT_MIGRATE flag before enabling notifs.  This is fine,
 * since we wouldn't receive any notifs that could lead to us migrating after we
 * set DONT_MIGRATE but before enable_notifs().  We need it to be in this order,
 * since we need to check messages after ~DONT_MIGRATE. */
void mcs_unlock_notifsafe(struct mcs_lock *lock, struct mcs_lock_qnode *qnode)
{
	mcs_lock_unlock(lock, qnode);
	uth_enable_notifs();
}

/* Preemption detection and recovering MCS locks. */
/* Old style.  Has trouble getting out of 'preempt/change-to storms' under
 * heavy contention and with preemption. */
void mcs_pdro_init(struct mcs_pdro_lock *lock)
{
	lock->lock = 0;
}

void mcs_pdro_fini(struct mcs_pdro_lock *lock)
{
}

/* Internal version of the locking function, doesn't care if notifs are
 * disabled.  While spinning, we'll check to see if other vcores involved in the
 * locking are running.  If we change to that vcore, we'll continue when our
 * vcore gets restarted.  If the change fails, it is because the vcore is
 * running, and we'll continue.
 *
 * It's worth noting that changing to another vcore won't hurt correctness.
 * Even if they are no longer the lockholder, they will be checking preemption
 * messages and will help break out of the deadlock.  So long as we don't
 * spin uncontrollably, we're okay. */
void __mcs_pdro_lock(struct mcs_pdro_lock *lock, struct mcs_pdro_qnode *qnode)
{
	struct mcs_pdro_qnode *predecessor;
	uint32_t pred_vcoreid;

	qnode->next = 0;
	cmb();	/* swap provides a CPU mb() */
	predecessor = atomic_swap_ptr((void**)&lock->lock, qnode);
	if (predecessor) {
		qnode->locked = 1;
		/* Read-in the vcoreid before releasing them.  We won't need to
		 * worry about their qnode memory being freed/reused (they can't
		 * til we fill in the 'next' slot), which is a bit of a
		 * performance win.  This also cuts down on cache-line
		 * contention when we ensure they run, which helps a lot too. */
		pred_vcoreid = ACCESS_ONCE(predecessor->vcoreid);
		wmb();	/* order the locked write before the next write */
		predecessor->next = qnode;
		/* no need for a wrmb(), since this will only get unlocked after
		 * they read our previous write */
		while (qnode->locked) {
			/* We don't know who the lock holder is (it hurts
			 * performance via 'true' sharing to track it)  Instead
			 * we'll make sure our pred is running, which trickles
			 * up to the lock holder. */
			ensure_vcore_runs(pred_vcoreid);
			cpu_relax();
		}
	}
}

/* Using the CAS style unlocks, since the usurper recovery is a real pain in the
 * ass */
void __mcs_pdro_unlock(struct mcs_pdro_lock *lock, struct mcs_pdro_qnode *qnode)
{
	uint32_t a_tail_vcoreid;
	/* Check if someone is already waiting on us to unlock */
	if (qnode->next == 0) {
		cmb();	/* no need for CPU mbs, since there's an atomic_cas() */
		/* If we're still the lock, just swap it with 0 (unlock) and
		 * return */
		if (atomic_cas_ptr((void**)&lock->lock, qnode, 0))
			return;
		/* Read in the tail (or someone who recently was the tail, but
		 * could now be farther up the chain), in prep for our spinning.
		 */
		a_tail_vcoreid = ACCESS_ONCE(lock->lock->vcoreid);
		/* We failed, someone is there and we are some (maybe a
		 * different) thread's pred.  Since someone else was waiting,
		 * they should have made themselves our next.  Spin (very
		 * briefly!) til it happens. */
		while (qnode->next == 0) {
			/* We need to get our next to run, but we don't know who
			 * they are.  If we make sure a tail is running, that
			 * will percolate up to make sure our qnode->next is
			 * running */
			ensure_vcore_runs(a_tail_vcoreid);
			cpu_relax();
		}
		/* Alpha wants a read_barrier_depends() here */
		/* Now that we have a next, unlock them */
		qnode->next->locked = 0;
	} else {
		/* mb()s necessary since we didn't call an atomic_swap() */
		/* need to make sure any previous writes don't pass unlocking */
		wmb();
		/* need to make sure any reads happen before the unlocking */
		rwmb();
		/* simply unlock whoever is next */
		qnode->next->locked = 0;
	}
}

/* Actual MCS-PDRO locks.  Don't worry about initializing any fields of qnode.
 * We'll do vcoreid here, and the locking code deals with the other fields */
void mcs_pdro_lock(struct mcs_pdro_lock *lock, struct mcs_pdro_qnode *qnode)
{
	/* Disable notifs, if we're an _M uthread */
	uth_disable_notifs();
	cmb();	/* in the off-chance the compiler wants to read vcoreid early */
	qnode->vcoreid = vcore_id();
	__mcs_pdro_lock(lock, qnode);
}

/* CAS-less unlock, not quite as efficient and will make sure every vcore runs
 * (since we don't have a convenient way to make sure our qnode->next runs
 * yet, other than making sure everyone runs).
 *
 * To use this without ensuring all vcores run, you'll need the unlock code to
 * save pred to a specific field in the qnode and check both its initial pred
 * as well as its run time pred (who could be an usurper).  It's all possible,
 * but a little more difficult to follow.  Also, I'm adjusting this comment
 * months after writing it originally, so it is probably not sufficient, but
 * necessary. */
void __mcs_pdro_unlock_no_cas(struct mcs_pdro_lock *lock,
                             struct mcs_pdro_qnode *qnode)
{
	struct mcs_pdro_qnode *old_tail, *usurper;

	/* Check if someone is already waiting on us to unlock */
	if (qnode->next == 0) {
		cmb(); /* no need for CPU mbs, since there's an atomic_swap() */
		/* Unlock it */
		old_tail = atomic_swap_ptr((void**)&lock->lock, 0);
		/* no one else was already waiting, so we successfully unlocked
		 * and can return */
		if (old_tail == qnode)
			return;
		/* someone else was already waiting on the lock (last one on the
		 * list), and we accidentally took them off.  Try and put it
		 * back. */
		usurper = atomic_swap_ptr((void*)&lock->lock, old_tail);
		/* since someone else was waiting, they should have made
		 * themselves our next.  spin (very briefly!) til it happens. */
		while (qnode->next == 0) {
			/* make sure old_tail isn't preempted.  best we can do
			 * for now is to make sure all vcores run, and thereby
			 * get our next. */
			for (int i = 0; i < max_vcores(); i++)
				ensure_vcore_runs(i);
			cpu_relax();
		}
		if (usurper) {
			/* an usurper is someone who snuck in before we could
			 * put the old tail back.  They now have the lock.
			 * Let's put whoever is supposed to be next as their
			 * next one. 
			 *
			 * First, we need to change our next's pred.  There's a
			 * slight race here, so our next will need to make sure
			 * both us and pred are done */
			/* I was trying to do something so we didn't need to
			 * ensure all vcores run, using more space in the qnode
			 * to figure out who our pred was a lock time (guessing
			 * actually, since there's a race, etc). */
			//qnode->next->pred = usurper;
			//wmb();
			usurper->next = qnode->next;
			/* could imagine another wmb() and a flag so our next
			 * knows to no longer check us too. */
		} else {
			/* No usurper meant we put things back correctly, so we
			 * should just pass the lock / unlock whoever is next */
			qnode->next->locked = 0;
		}
	} else {
		/* mb()s necessary since we didn't call an atomic_swap() */
		/* need to make sure any previous writes don't pass unlocking */
		wmb();
		/* need to make sure any reads happen before the unlocking */
		rwmb();
		/* simply unlock whoever is next */
		qnode->next->locked = 0;
	}
}

void mcs_pdro_unlock(struct mcs_pdro_lock *lock, struct mcs_pdro_qnode *qnode)
{
	__mcs_pdro_unlock(lock, qnode);
	/* Enable notifs, if we're an _M uthread */
	uth_enable_notifs();
}

/* New style: under heavy contention with preemption, they won't enter the
 * 'preempt/change_to storm' that can happen to PDRs, at the cost of some
 * performance.  This is the default. */
void mcs_pdr_init(struct mcs_pdr_lock *lock)
{
	int ret;

	lock->lock = 0;
	lock->lockholder_vcoreid = MCSPDR_NO_LOCKHOLDER;
	ret = posix_memalign((void**)&lock->qnodes,
	                     __alignof__(struct mcs_pdr_qnode),
	                     sizeof(struct mcs_pdr_qnode) * max_vcores());
	assert(!ret);
}

void mcs_pdr_fini(struct mcs_pdr_lock *lock)
{
	free(lock->qnodes);
}

/* Similar to the original PDR lock, this tracks the lockholder for better
 * recovery from preemptions.  Under heavy contention, changing to the
 * lockholder instead of pred makes it more likely to have a vcore outside the
 * MCS chain handle the preemption.  If that never happens, performance will
 * suffer.
 *
 * Simply checking the lockholder causes a lot of unnecessary traffic, so we
 * first look for signs of preemption in read-mostly locations (by comparison,
 * the lockholder changes on every lock/unlock).
 *
 * We also use the "qnodes are in the lock" style, which is slightly slower than
 * using the stack in regular MCS/MCSPDR locks, but it speeds PDR up a bit by
 * not having to read other qnodes' memory to determine their vcoreid.  The
 * slowdown may be due to some weird caching/prefetch settings (like Adjacent
 * Cacheline Prefetch).
 *
 * Note that these locks, like all PDR locks, have opportunities to accidentally
 * ensure some vcore runs that isn't in the chain.  Whenever we read lockholder
 * or even pred, that particular vcore might subsequently unlock and then get
 * preempted (or change_to someone else) before we ensure they run.  If this
 * happens and there is another VC in the MCS chain, it will make sure the right
 * cores run.  If there are no other vcores in the chain, it is up to the rest
 * of the vcore/event handling system to deal with this, which should happen
 * when one of the other vcores handles the preemption message generated by our
 * change_to. */
void __mcs_pdr_lock(struct mcs_pdr_lock *lock, struct mcs_pdr_qnode *qnode)
{
	struct mcs_pdr_qnode *predecessor;
	uint32_t pred_vcoreid;
	struct mcs_pdr_qnode *qnode0 = qnode - vcore_id();
	seq_ctr_t seq;
	qnode->next = 0;
	cmb();	/* swap provides a CPU mb() */
	predecessor = atomic_swap_ptr((void**)&lock->lock, qnode);
	if (predecessor) {
		qnode->locked = 1;
		/* can compute this whenever */
		pred_vcoreid = predecessor - qnode0;
		wmb();	/* order the locked write before the next write */
		predecessor->next = qnode;
		seq = ACCESS_ONCE(__procinfo.coremap_seqctr);
		/* no need for a wrmb(), since this will only get unlocked after
		 * they read our pred->next write */
		while (qnode->locked) {
			/* Check to see if anything is amiss.  If someone in the
			 * chain is preempted, then someone will notice.  Simply
			 * checking our pred isn't that great of an indicator of
			 * preemption.  The reason is that the offline vcore is
			 * most likely the lockholder (under heavy lock
			 * contention), and we want someone farther back in the
			 * chain to notice (someone that will stay preempted
			 * long enough for a vcore outside the chain to recover
			 * them).  Checking the seqctr will tell us of any
			 * preempts since we started, so if a storm starts while
			 * we're spinning, we can join in and try to save the
			 * lockholder before its successor gets it.
			 *
			 * Also, if we're the lockholder, then we need to let
			 * our pred run so they can hand us the lock. */
			if (vcore_is_preempted(pred_vcoreid) ||
			    seq != __procinfo.coremap_seqctr) {
				/* Note that we don't normally ensure our *pred*
				 * runs. */
				if (lock->lockholder_vcoreid ==
				    MCSPDR_NO_LOCKHOLDER ||
				    lock->lockholder_vcoreid == vcore_id())
					ensure_vcore_runs(pred_vcoreid);
				else
					ensure_vcore_runs(
						lock->lockholder_vcoreid);
			}
			cpu_relax();
		}
	} else {
		lock->lockholder_vcoreid = vcore_id();
	}
}

void __mcs_pdr_unlock(struct mcs_pdr_lock *lock, struct mcs_pdr_qnode *qnode)
{
	uint32_t a_tail_vcoreid;
	struct mcs_pdr_qnode *qnode0 = qnode - vcore_id();

	/* Check if someone is already waiting on us to unlock */
	if (qnode->next == 0) {
		cmb();	/* no need for CPU mbs, since there's an atomic_cas() */
		/* If we're still the lock, just swap it with 0 (unlock) and
		 * return */
		if (atomic_cas_ptr((void**)&lock->lock, qnode, 0)) {
			/* This is racy with the new lockholder.  it's possible
			 * that we'll clobber their legit write, though it
			 * doesn't actually hurt correctness.  it'll get sorted
			 * out on the next unlock. */
			lock->lockholder_vcoreid = MCSPDR_NO_LOCKHOLDER;
			return;
		}
		/* Get the tail (or someone who recently was the tail, but could
		 * now be farther up the chain), in prep for our spinning.
		 * Could do an ACCESS_ONCE on lock->lock */
		a_tail_vcoreid = lock->lock - qnode0;
		/* We failed, someone is there and we are some (maybe a
		 * different) thread's pred.  Since someone else was waiting,
		 * they should have made themselves our next.  Spin (very
		 * briefly!) til it happens. */
		while (qnode->next == 0) {
			/* We need to get our next to run, but we don't know who
			 * they are.  If we make sure a tail is running, that
			 * will percolate up to make sure our qnode->next is
			 * running.
			 *
			 * But first, we need to tell everyone that there is no
			 * specific lockholder.  lockholder_vcoreid is a
			 * short-circuit on the "walk the chain" PDR.  Normally,
			 * that's okay.  But now we need to make sure everyone
			 * is walking the chain from a_tail up to our pred. */
			lock->lockholder_vcoreid = MCSPDR_NO_LOCKHOLDER;
			ensure_vcore_runs(a_tail_vcoreid);
			cpu_relax();
		}
		/* Alpha wants a read_barrier_depends() here */
		lock->lockholder_vcoreid = qnode->next - qnode0;
		wmb();	/* order the vcoreid write before the unlock */
		qnode->next->locked = 0;
	} else {
		/* Note we're saying someone else is the lockholder, though we
		 * still are the lockholder until we unlock the next qnode.  Our
		 * next knows that if it sees itself is the lockholder, that it
		 * needs to make sure we run. */
		lock->lockholder_vcoreid = qnode->next - qnode0;
		/* mb()s necessary since we didn't call an atomic_swap() */
		/* need to make sure any previous writes don't pass unlocking */
		wmb();
		/* need to make sure any reads happen before the unlocking */
		rwmb();
		/* simply unlock whoever is next */
		qnode->next->locked = 0;
	}
}

void mcs_pdr_lock(struct mcs_pdr_lock *lock)
{
	uth_disable_notifs();
	cmb();	/* in the off-chance the compiler wants to read vcoreid early */
	__mcs_pdr_lock(lock, &lock->qnodes[vcore_id()]);
}

void mcs_pdr_unlock(struct mcs_pdr_lock *lock)
{
	__mcs_pdr_unlock(lock, &lock->qnodes[vcore_id()]);
	uth_enable_notifs();
}
