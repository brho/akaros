/* Copyright (c) 2013 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Atomic pipes.  Multi-reader, multi-writer pipes, similar to sys_pipe except
 * that they operate on fixed sized chunks of data.
 *
 * A note on broadcast wakeups.  We broadcast in a few places.  If we don't,
 * then all paths (like error paths) will have to signal.  Not a big deal
 * either way, but just need to catch all the cases.  Other non-obvious
 * cases are that read and write methods need to wake other readers and
 * writers (in the absence of a broadcast wakeup) */

#include <apipe.h>
#include <ros/ring_buffer.h>
#include <string.h>
#include <stdio.h>

void apipe_init(struct atomic_pipe *ap, void *buf, size_t buf_sz,
                size_t elem_sz)
{
	ap->ap_buf = buf;
	/* power of two number of elements in the ring. */
	ap->ap_ring_sz = ROUNDDOWNPWR2(buf_sz / elem_sz);
	ap->ap_elem_sz = elem_sz;
	ap->ap_rd_off = 0;
	ap->ap_wr_off = 0;
	ap->ap_nr_readers = 1;
	ap->ap_nr_writers = 1;
	/* Three CVs, all using the same lock. */
	spinlock_init(&ap->ap_lock);
	cv_init_with_lock(&ap->ap_priority_reader, &ap->ap_lock);
	cv_init_with_lock(&ap->ap_general_readers, &ap->ap_lock);
	cv_init_with_lock(&ap->ap_writers, &ap->ap_lock);
	ap->ap_has_priority_reader = FALSE;
}

void apipe_open_reader(struct atomic_pipe *ap)
{
	spin_lock(&ap->ap_lock);
	ap->ap_nr_readers++;
	spin_unlock(&ap->ap_lock);
}

void apipe_open_writer(struct atomic_pipe *ap)
{
	spin_lock(&ap->ap_lock);
	ap->ap_nr_writers++;
	spin_unlock(&ap->ap_lock);
}

/* Helper: Wake the appropriate readers.  When there's a priority reader, only
 * that one wakes up.  It's up to the priority reader to wake the other readers,
 * by clearing has_prior and calling this again. */
static void __apipe_wake_readers(struct atomic_pipe *ap)
{
	if (ap->ap_has_priority_reader)
		__cv_signal(&ap->ap_priority_reader);
	else
		__cv_broadcast(&ap->ap_general_readers);
}

/* When closing, there might be others blocked waiting for us.  For example,
 * a writer could have blocked on a full pipe, waiting for us to read.  Instead
 * of reading, the last reader closes.  The writer needs to be woken up so it
 * can return 0. */
void apipe_close_reader(struct atomic_pipe *ap)
{
	spin_lock(&ap->ap_lock);
	ap->ap_nr_readers--;
	__cv_broadcast(&ap->ap_writers);
	spin_unlock(&ap->ap_lock);
}

void apipe_close_writer(struct atomic_pipe *ap)
{
	spin_lock(&ap->ap_lock);
	ap->ap_nr_writers--;
	__apipe_wake_readers(ap);
	spin_unlock(&ap->ap_lock);
}

/* read a pipe that is already locked. */
int apipe_read_locked(struct atomic_pipe *ap, void *buf, size_t nr_elem)
{
	size_t rd_idx;
	int nr_copied = 0;

	for (int i = 0; i < nr_elem; i++) {
		/* readers that call read_locked directly might have failed to
		 * check for emptiness, so we'll double check early. */
		if (__ring_empty(ap->ap_wr_off, ap->ap_rd_off))
			break;
		/* power of 2 elements in the ring buffer, index is the lower n
		 * bits */
		rd_idx = ap->ap_rd_off & (ap->ap_ring_sz - 1);
		memcpy(buf, ap->ap_buf + rd_idx * ap->ap_elem_sz,
		       ap->ap_elem_sz);
		ap->ap_rd_off++;
		buf += ap->ap_elem_sz;
		nr_copied++;
	}
	/* We could have multiple writers blocked.  Just broadcast for them all.
	 * Alternatively, we could signal one, and then it's on the writers to
	 * signal further writers (see the note at the top of this file). */
	__cv_broadcast(&ap->ap_writers);
	return nr_copied;
}


int apipe_read(struct atomic_pipe *ap, void *buf, size_t nr_elem)
{
	size_t rd_idx;
	int nr_copied = 0;

	spin_lock(&ap->ap_lock);
	/* Need to wait til the priority reader is gone, and the ring isn't
	 * empty.  If we do this as two steps, (either of priority check or
	 * empty check first), there's a chance the second one will fail, and
	 * when we sleep and wake up, the first condition could have changed.
	 * (An alternative would be to block priority readers too, by promoting
	 * ourselves to a priority reader). */
	while (ap->ap_has_priority_reader ||
	       __ring_empty(ap->ap_wr_off, ap->ap_rd_off)) {
		if (!ap->ap_nr_writers) {
			spin_unlock(&ap->ap_lock);
			return 0;
		}
		cv_wait(&ap->ap_general_readers);
		cpu_relax();
	}
	/* This read call wakes up writers */
	nr_copied = apipe_read_locked(ap, buf, nr_elem);
	/* If the writer didn't broadcast, we'd need to wake other readers
	 * (imagine a long queue of blocked readers, and a queue filled by one
	 * massive write).  (same with the error case). */
	spin_unlock(&ap->ap_lock);
	return nr_copied;
}

int apipe_write(struct atomic_pipe *ap, void *buf, size_t nr_elem)
{
	size_t wr_idx;
	int nr_copied = 0;

	spin_lock(&ap->ap_lock);
	/* not sure if we want to check for readers first or not */
	while (__ring_full(ap->ap_ring_sz, ap->ap_wr_off, ap->ap_rd_off)) {
		if (!ap->ap_nr_readers) {
			spin_unlock(&ap->ap_lock);
			return 0;
		}
		cv_wait(&ap->ap_writers);
		cpu_relax();
	}
	for (int i = 0; i < nr_elem; i++) {
		/* power of 2 elements in the ring buffer, index is the lower n
		 * bits */
		wr_idx = ap->ap_wr_off & (ap->ap_ring_sz - 1);
		memcpy(ap->ap_buf + wr_idx * ap->ap_elem_sz, buf,
		       ap->ap_elem_sz);
		ap->ap_wr_off++;
		buf += ap->ap_elem_sz;
		nr_copied++;
		if (__ring_full(ap->ap_ring_sz, ap->ap_wr_off, ap->ap_rd_off))
			break;
	}
	/* We only need to wake readers, since the reader that woke us used a
	 * broadcast.  o/w, we'd need to wake the next writer.  (same goes for
	 * the error case). */
	__apipe_wake_readers(ap);
	spin_unlock(&ap->ap_lock);
	return nr_copied;
}

void *apipe_head(struct atomic_pipe *ap)
{
	if (__ring_empty(ap->ap_wr_off, ap->ap_rd_off))
		return 0;
	return ap->ap_buf +
	       (ap->ap_rd_off & (ap->ap_ring_sz - 1)) * ap->ap_elem_sz;
}

/*
 * Read data from the pipe until a condition is satisfied.
 * f is the function that determines the condition. f saves its
 * state in arg. When f returns non-zero, this function exits,
 * and returns the value to its caller. Note that f can return -1
 * to indicate an error. But returning zero will keep you trapped in
 * this function. The intent here is to ensure one-reader-at-a-time
 * operation.
 */
int apipe_read_cond(struct atomic_pipe *ap,
		    int(*f)(struct atomic_pipe *pipe, void *arg), void *arg)
{
	size_t rd_idx;
	int ret;

	spin_lock(&ap->ap_lock);
	/* Can only have one priority reader at a time.  Wait our turn. */
	while (ap->ap_has_priority_reader) {
		cv_wait(&ap->ap_general_readers);
		cpu_relax();
	}
	ap->ap_has_priority_reader = TRUE;
	while (1) {
		/* Each time there is a need to check the pipe, call
		 * f. f will maintain its state in arg. It is expected that f
		 * will dequeue elements from the pipe as they are available.
		 * N.B. this is being done for protocols like IPV4 that can
		 * fragment an RPC request. For IPV6, it is likely that this
		 * will end up looking like a blocking read. Thus was it ever
		 * with legacy code. F is supposed to call apipe_read_locked().
		 */
		ret = f(ap, arg);
		if (ret)
			break;
		/* if nr_writers goes to zero, that's bad.  return -1 because
		 * they're going to have to clean up.  We should have been able
		 * to call f once though, to pull out any remaining elements.
		 * The main concern here is sleeping on the cv when no one (no
		 * writers) will wake us. */
		if (!ap->ap_nr_writers) {
			ret = -1;
			goto out;
		}
		cv_wait(&ap->ap_priority_reader);
		cpu_relax();
	}
out:
	/* All out paths need to wake other readers.  When we were woken up,
	 * there was no broadcast sent to the other readers.  Plus, there may be
	 * other potential priority readers. */
	ap->ap_has_priority_reader = FALSE;
	__apipe_wake_readers(ap);
	/* FYI, writers were woken up after an actual read.  If we had an error
	 * (ret == -1), there should be no writers. */
	spin_unlock(&ap->ap_lock);
	return ret;
}
