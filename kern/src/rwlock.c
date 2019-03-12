/* Copyright (c) 2013 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Reader-writer queue locks (sleeping locks).
 *
 * We favor readers when reading, meaning new readers can move ahead of writers.
 * Ex: If i have some readers, then a writer, clearly the writer blocks.  If
 * more readers come in, they can just come in and the presence of the writer
 * doesn't stop them.
 *
 * You get potential writer starvation, but you also get the property that
 * if a thread holds a read-lock, that thread can grab the same reader
 * lock again.  A more general statement would be "if some reader holds
 * a rwlock, then any other thread (including itself) can get an rlock".
 *
 * Similarly, writers favor other writers.  So if a writer is unlocking, it'll
 * pass the lock to another writer first.  Here, there is potential reader
 * starvation.
 *
 * We also pass locks, instead of letting recently woken threads fight for it.
 * In the case of a reader wakeup, we know that they all will wake up and read.
 * Instead of having them fight for a lock and then incref, the waker (the last
 * writer) will up the count and just wake everyone.
 *
 * This also helps when a writer wants to favor another writer.  If we didn't
 * pass the lock, then a new reader could squeeze in after our old writer
 * signalled the new writer.  Even worse, in this case, the readers that we
 * didn't wake up are still sleeping, even though a reader now holds the lock.
 * It won't deadlock, (since eventually the reader will wake the writer, who
 * wakes the old readers) but it breaks the notion of a RW lock a bit. */

#include <rwlock.h>
#include <atomic.h>
#include <kthread.h>

void rwinit(struct rwlock *rw_lock)
{
	spinlock_init(&rw_lock->lock);
	atomic_init(&rw_lock->nr_readers, 0);
	rw_lock->writing = FALSE;
	cv_init_with_lock(&rw_lock->readers, &rw_lock->lock);
	cv_init_with_lock(&rw_lock->writers, &rw_lock->lock);
}

void rlock(struct rwlock *rw_lock)
{
	/* If we already have a reader, we can just increment and return.  This
	 * is the only access to nr_readers outside the lock.  All locked uses
	 * need to be aware that the nr could be concurrently increffed (unless
	 * it is 0). */
	if (atomic_add_not_zero(&rw_lock->nr_readers, 1))
		return;
	/* Here's an alternate style: the broadcaster (a writer) will up the
	 * readers count and just wake us.  All readers just proceed, instead of
	 * fighting to lock and up the count.  The writer 'passed' the rlock to
	 * us. */
	spin_lock(&rw_lock->lock);
	if (rw_lock->writing) {
		cv_wait_and_unlock(&rw_lock->readers);
		return;
	}
	atomic_inc(&rw_lock->nr_readers);
	spin_unlock(&rw_lock->lock);
}

bool canrlock(struct rwlock *rw_lock)
{
	if (atomic_add_not_zero(&rw_lock->nr_readers, 1))
		return TRUE;
	spin_lock(&rw_lock->lock);
	if (rw_lock->writing) {
		spin_unlock(&rw_lock->lock);
		return FALSE;
	}
	atomic_inc(&rw_lock->nr_readers);
	spin_unlock(&rw_lock->lock);
	return TRUE;
}

void runlock(struct rwlock *rw_lock)
{
	spin_lock(&rw_lock->lock);
	/* sub and test will tell us if we got the refcnt to 0, atomically.
	 * syncing with the atomic_add_not_zero of new readers.  Since we're
	 * passing the lock, we need to make sure someone is sleeping.  Contrast
	 * to the wunlock, where we can just blindly broadcast and add
	 * (potentially == 0). */
	if (atomic_sub_and_test(&rw_lock->nr_readers, 1) &&
	        rw_lock->writers.nr_waiters) {
		/* passing the lock to the one writer we signal. */
		rw_lock->writing = TRUE;
		__cv_signal(&rw_lock->writers);
	}
	spin_unlock(&rw_lock->lock);
}

void wlock(struct rwlock *rw_lock)
{
	spin_lock(&rw_lock->lock);
	if (atomic_read(&rw_lock->nr_readers) || rw_lock->writing) {
		/* If we slept, the lock was passed to us */
		cv_wait_and_unlock(&rw_lock->writers);
		return;
	}
	rw_lock->writing = TRUE;
	spin_unlock(&rw_lock->lock);
}

void wunlock(struct rwlock *rw_lock)
{
	/* Pass the lock to another writer (we leave writing = TRUE) */
	spin_lock(&rw_lock->lock);
	if (rw_lock->writers.nr_waiters) {
		/* Just waking one */
		__cv_signal(&rw_lock->writers);
		spin_unlock(&rw_lock->lock);
		return;
	}
	rw_lock->writing = FALSE;
	atomic_set(&rw_lock->nr_readers, rw_lock->readers.nr_waiters);
	__cv_broadcast(&rw_lock->readers);
	spin_unlock(&rw_lock->lock);
}
