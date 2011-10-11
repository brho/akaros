#include <vcore.h>
#include <mcs.h>
#include <arch/atomic.h>
#include <string.h>
#include <stdlib.h>
#include <uthread.h>

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

