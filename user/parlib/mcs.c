#include <vcore.h>
#include <mcs.h>
#include <arch/atomic.h>
#include <string.h>
#include <stdlib.h>
#include <uthread.h>
#include <parlib.h>

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
		/* no need for a wrmb(), since this will only get unlocked after they
		 * read our previous write */
		while (qnode->locked)
			cpu_relax();
	}
	cmb();	/* just need a cmb, the swap handles the CPU wmb/wrmb() */
}

void mcs_lock_unlock(struct mcs_lock *lock, struct mcs_lock_qnode *qnode)
{
	/* Check if someone is already waiting on us to unlock */
	if (qnode->next == 0) {
		cmb();	/* no need for CPU mbs, since there's an atomic_swap() */
		/* Unlock it */
		mcs_lock_qnode_t *old_tail = mcs_qnode_swap(&lock->lock,0);
		/* no one else was already waiting, so we successfully unlocked and can
		 * return */
		if (old_tail == qnode)
			return;
		/* someone else was already waiting on the lock (last one on the list),
		 * and we accidentally took them off.  Try and put it back. */
		mcs_lock_qnode_t *usurper = mcs_qnode_swap(&lock->lock,old_tail);
		/* since someone else was waiting, they should have made themselves our
		 * next.  spin (very briefly!) til it happens. */
		while (qnode->next == 0)
			cpu_relax();
		if (usurper) {
			/* an usurper is someone who snuck in before we could put the old
			 * tail back.  They now have the lock.  Let's put whoever is
			 * supposed to be next as their next one. */
			usurper->next = qnode->next;
		} else {
			/* No usurper meant we put things back correctly, so we should just
			 * pass the lock / unlock whoever is next */
			qnode->next->locked = 0;
		}
	} else {
		/* mb()s necessary since we didn't call an atomic_swap() */
		wmb();	/* need to make sure any previous writes don't pass unlocking */
		rwmb();	/* need to make sure any reads happen before the unlocking */
		/* simply unlock whoever is next */
		qnode->next->locked = 0;
	}
}

/* CAS style mcs locks, kept around til we use them.  We're using the
 * usurper-style, since RISCV and SPARC both don't have a real CAS. */
void mcs_lock_unlock_cas(struct mcs_lock *lock, struct mcs_lock_qnode *qnode)
{
	/* Check if someone is already waiting on us to unlock */
	if (qnode->next == 0) {
		cmb();	/* no need for CPU mbs, since there's an atomic_cas() */
		/* If we're still the lock, just swap it with 0 (unlock) and return */
		if (atomic_cas_ptr((void**)&lock->lock, qnode, 0))
			return;
		/* We failed, someone is there and we are some (maybe a different)
		 * thread's pred.  Since someone else was waiting, they should have made
		 * themselves our next.  Spin (very briefly!) til it happens. */
		while (qnode->next == 0)
			cpu_relax();
		/* Alpha wants a read_barrier_depends() here */
		/* Now that we have a next, unlock them */
		qnode->next->locked = 0;
	} else {
		/* mb()s necessary since we didn't call an atomic_swap() */
		wmb();	/* need to make sure any previous writes don't pass unlocking */
		rwmb();	/* need to make sure any reads happen before the unlocking */
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

// MCS dissemination barrier!
int mcs_barrier_init(mcs_barrier_t* b, size_t np)
{
	if(np > max_vcores())
		return -1;
	b->allnodes = (mcs_dissem_flags_t*)malloc(np*sizeof(mcs_dissem_flags_t));
	memset(b->allnodes,0,np*sizeof(mcs_dissem_flags_t));
	b->nprocs = np;

	b->logp = (np & (np-1)) != 0;
	while(np >>= 1)
		b->logp++;

	size_t i,k;
	for(i = 0; i < b->nprocs; i++)
	{
		b->allnodes[i].parity = 0;
		b->allnodes[i].sense = 1;

		for(k = 0; k < b->logp; k++)
		{
			size_t j = (i+(1<<k)) % b->nprocs;
			b->allnodes[i].partnerflags[0][k] = &b->allnodes[j].myflags[0][k];
			b->allnodes[i].partnerflags[1][k] = &b->allnodes[j].myflags[1][k];
		} 
	}

	return 0;
}

void mcs_barrier_wait(mcs_barrier_t* b, size_t pid)
{
	mcs_dissem_flags_t* localflags = &b->allnodes[pid];
	size_t i;
	for(i = 0; i < b->logp; i++)
	{
		*localflags->partnerflags[localflags->parity][i] = localflags->sense;
		while(localflags->myflags[localflags->parity][i] != localflags->sense);
	}
	if(localflags->parity)
		localflags->sense = 1-localflags->sense;
	localflags->parity = 1-localflags->parity;
}

/* Preemption detection and recovering MCS locks.  These are memory safe ones.
 * In the future, we can make ones that you pass the qnode to, so long as you
 * never free the qnode storage (stacks) */
void mcs_pdr_init(struct mcs_pdr_lock *lock)
{
	lock->lock = 0;
	lock->lock_holder = 0;
	lock->vc_qnodes = malloc(sizeof(struct mcs_pdr_qnode) * max_vcores());
	assert(lock->vc_qnodes);
	for (int i = 0; i < max_vcores(); i++)
		lock->vc_qnodes[i].vcoreid = i;
}

void mcs_pdr_fini(struct mcs_pdr_lock *lock)
{
	assert(lock->vc_qnodes);
	free(lock->vc_qnodes);
}

/* Helper, will make sure the vcore owning qnode is running.  If we change to
 * that vcore, we'll continue when our vcore gets restarted.  If the change
 * fails, it is because the vcore is running, and we'll continue.
 *
 * It's worth noting that changing to another vcore won't hurt correctness.
 * Even if they are no longer the lockholder, they will be checking preemption
 * messages and will help break out of the deadlock.  So long as we don't
 * wastefully spin, we're okay. */
void __ensure_qnode_runs(struct mcs_pdr_qnode *qnode)
{
	assert(qnode);
	if (vcore_is_preempted(qnode->vcoreid)) {
		assert(!vcore_is_mapped(qnode->vcoreid));
		/* We want to recover them from preemption.  Since we know they have
		 * notifs disabled, they will need to be directly restarted, so we can
		 * skip the other logic and cut straight to the sys_change_vcore() */
		sys_change_vcore(qnode->vcoreid, FALSE);
		cmb();
	}
}

/* Internal version of the locking function, doesn't care about storage of qnode
 * or if notifs are disabled. */
void __mcs_pdr_lock(struct mcs_pdr_lock *lock, struct mcs_pdr_qnode *qnode)
{
	struct mcs_pdr_qnode *predecessor;
	/* Now the actual lock */
	qnode->next = 0;
	cmb();	/* swap provides a CPU mb() */
	predecessor = atomic_swap_ptr((void**)&lock->lock, qnode);
	if (predecessor) {
		qnode->locked = 1;
		wmb();
		predecessor->next = qnode;
		/* no need for a wrmb(), since this will only get unlocked after they
		 * read our previous write */
		while (qnode->locked) {
			/* Ideally, we know who the lock holder is, and we'll make sure they
			 * run.  If not, we'll make sure our pred is running, which trickles
			 * up to the lock holder, if it isn't them. */
			if (lock->lock_holder)
				__ensure_qnode_runs(lock->lock_holder);
			else
				__ensure_qnode_runs(predecessor);
			cpu_relax();
		}
	}
	cmb();	/* just need a cmb, the swap handles the CPU wmb/wrmb() */
	/* publish ourselves as the lock holder (optimization) */
	lock->lock_holder = qnode;	/* mbs() handled by the cmb/swap */
}

/* Using the CAS style unlocks, since the usurper recovery is a real pain in the
 * ass */
void __mcs_pdr_unlock(struct mcs_pdr_lock *lock, struct mcs_pdr_qnode *qnode)
{
	struct mcs_pdr_qnode *a_tail;
	/* Clear us from being the lock holder */
	lock->lock_holder = 0;	/* mbs() are covered by the cmb/cas and the wmb */
	/* Check if someone is already waiting on us to unlock */
	if (qnode->next == 0) {
		cmb();	/* no need for CPU mbs, since there's an atomic_cas() */
		/* If we're still the lock, just swap it with 0 (unlock) and return */
		if (atomic_cas_ptr((void**)&lock->lock, qnode, 0))
			return;
		cmb();	/* need to read a fresh tail.  the CAS was a CPU mb */
		/* Read in the tail (or someone who recent was the tail, but could now
		 * be farther up the chain), in prep for our spinning. */
		a_tail = lock->lock;
		/* We failed, someone is there and we are some (maybe a different)
		 * thread's pred.  Since someone else was waiting, they should have made
		 * themselves our next.  Spin (very briefly!) til it happens. */
		while (qnode->next == 0) {
			/* We need to get our next to run, but we don't know who they are.
			 * If we make sure a tail is running, that will percolate up to make
			 * sure our qnode->next is running */
			__ensure_qnode_runs(a_tail);
			/* Arguably, that reads new tails each time, but it'll still work
			 * for this rare case */
			cpu_relax();
		}
		/* Alpha wants a read_barrier_depends() here */
		/* Now that we have a next, unlock them */
		qnode->next->locked = 0;
	} else {
		/* mb()s necessary since we didn't call an atomic_swap() */
		wmb();	/* need to make sure any previous writes don't pass unlocking */
		rwmb();	/* need to make sure any reads happen before the unlocking */
		/* simply unlock whoever is next */
		qnode->next->locked = 0;
	}
}

/* Actual MCS-PDR locks.  These use memory in the lock for their qnodes, though
 * the internal locking code doesn't care where the qnodes come from: so long as
 * they are not freed and can stand a random read of vcoreid. */
void mcs_pdr_lock(struct mcs_pdr_lock *lock)
{
	struct mcs_pdr_qnode *qnode;
	/* Disable notifs, if we're an _M uthread */
	uth_disable_notifs();
	/* Get our qnode from the array.  vcoreid was preset, and the other fields
	 * get handled by the lock */
	qnode = &lock->vc_qnodes[vcore_id()];
	assert(qnode->vcoreid == vcore_id());	/* sanity */
	__mcs_pdr_lock(lock, qnode);
}

void mcs_pdr_unlock(struct mcs_pdr_lock *lock)
{
	struct mcs_pdr_qnode *qnode = &lock->vc_qnodes[vcore_id()];
	assert(qnode->vcoreid == vcore_id());	/* sanity */
	__mcs_pdr_unlock(lock, qnode);
	/* Enable notifs, if we're an _M uthread */
	uth_enable_notifs();
}

#if 0
/* We don't actually use this.  To use this, you'll need the unlock code to save
 * pred to a specific field in the qnode and check both its initial pred as well
 * as its run time pred (who could be an usurper).  It's all possible, but a
 * little more difficult to follow. */
void __mcs_pdr_unlock_no_cas(struct mcs_pdr_lock *lock,
                             struct mcs_pdr_qnode *qnode)
{
	struct mcs_pdr_qnode *old_tail, *usurper;
	/* Check if someone is already waiting on us to unlock */
	if (qnode->next == 0) {
		cmb();	/* no need for CPU mbs, since there's an atomic_swap() */
		/* Unlock it */
		old_tail = atomic_swap_ptr((void**)&lock->lock, 0);
		/* no one else was already waiting, so we successfully unlocked and can
		 * return */
		if (old_tail == qnode)
			return;
		/* someone else was already waiting on the lock (last one on the list),
		 * and we accidentally took them off.  Try and put it back. */
		usurper = atomic_swap_ptr((void*)&lock->lock, old_tail);
		/* since someone else was waiting, they should have made themselves our
		 * next.  spin (very briefly!) til it happens. */
		while (qnode->next == 0) {
			/* make sure old_tail isn't preempted */

			cpu_relax();
		}
		if (usurper) {
			/* an usurper is someone who snuck in before we could put the old
			 * tail back.  They now have the lock.  Let's put whoever is
			 * supposed to be next as their next one. 
			 *
			 * First, we need to change our next's pred.  There's a slight race
			 * here, so our next will need to make sure both us and pred are
			 * done */
			qnode->next->pred = usurper;
			wmb();
			usurper->next = qnode->next;
			/* could imagine another wmb() and a flag so our next knows to no
			 * longer check us too. */
		} else {
			/* No usurper meant we put things back correctly, so we should just
			 * pass the lock / unlock whoever is next */
			qnode->next->locked = 0;
		}
	} else {
		/* mb()s necessary since we didn't call an atomic_swap() */
		wmb();	/* need to make sure any previous writes don't pass unlocking */
		rwmb();	/* need to make sure any reads happen before the unlocking */
		/* simply unlock whoever is next */
		qnode->next->locked = 0;
	}
}
#endif
