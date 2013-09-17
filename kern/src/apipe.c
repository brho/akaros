/* Copyright (c) 2013 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Atomic pipes.  Multi-reader, multi-writer pipes, similar to sys_pipe except
 * that they operate on fixed sized chunks of data. */

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
	cv_init(&ap->ap_cv);
}

void apipe_open_reader(struct atomic_pipe *ap)
{
	cv_lock(&ap->ap_cv);
	ap->ap_nr_readers++;
	cv_unlock(&ap->ap_cv);
}

void apipe_open_writer(struct atomic_pipe *ap)
{
	cv_lock(&ap->ap_cv);
	ap->ap_nr_writers++;
	cv_unlock(&ap->ap_cv);
}

/* When closing, there might be others blocked waiting for us.  For example,
 * a writer could have blocked on a full pipe, waiting for us to read.  Instead
 * of reading, the last reader closes.  The writer needs to be woken up so it
 * can return 0. */
void apipe_close_reader(struct atomic_pipe *ap)
{
	cv_lock(&ap->ap_cv);
	ap->ap_nr_readers--;
	__cv_broadcast(&ap->ap_cv);
	cv_unlock(&ap->ap_cv);
}

void apipe_close_writer(struct atomic_pipe *ap)
{
	cv_lock(&ap->ap_cv);
	ap->ap_nr_writers--;
	__cv_broadcast(&ap->ap_cv);
	cv_unlock(&ap->ap_cv);
}

/* read a pipe that is already locked. */
int apipe_read_locked(struct atomic_pipe *ap, void *buf, size_t nr_elem)
{
	size_t rd_idx;
	int nr_copied = 0;

	for (int i = 0; i < nr_elem; i++) {
		/* power of 2 elements in the ring buffer, index is the lower n bits */
		rd_idx = ap->ap_rd_off & (ap->ap_ring_sz - 1);
		memcpy(buf, ap->ap_buf + rd_idx * ap->ap_elem_sz, ap->ap_elem_sz);
		ap->ap_rd_off++;
		buf += ap->ap_elem_sz;
		nr_copied++;
		if (__ring_empty(ap->ap_wr_off, ap->ap_rd_off))
			break;
	}
	return nr_copied;
}


int apipe_read(struct atomic_pipe *ap, void *buf, size_t nr_elem)
{
	size_t rd_idx;
	int nr_copied = 0;

	cv_lock(&ap->ap_cv);
	while (__ring_empty(ap->ap_wr_off, ap->ap_rd_off)) {
		if (!ap->ap_nr_writers) {
			cv_unlock(&ap->ap_cv);
			return 0;
		}
		cv_wait(&ap->ap_cv);
		cpu_relax();
	}
	nr_copied = apipe_read_locked(ap, buf, nr_elem);
	/* since we're using just one CV, there could be readers and writers blocked
	 * on it.  need to wake them all, to make sure we signal any blocked
	 * writers. */
	__cv_broadcast(&ap->ap_cv);
	cv_unlock(&ap->ap_cv);
	return nr_copied;
}

int apipe_write(struct atomic_pipe *ap, void *buf, size_t nr_elem)
{
	size_t wr_idx;
	int nr_copied = 0;

	cv_lock(&ap->ap_cv);
	/* not sure if we want to check for readers first or not */
	while (__ring_full(ap->ap_ring_sz, ap->ap_wr_off, ap->ap_rd_off)) {
		if (!ap->ap_nr_readers) {
			cv_unlock(&ap->ap_cv);
			return 0;
		}
		cv_wait(&ap->ap_cv);
		cpu_relax();
	}
	for (int i = 0; i < nr_elem; i++) {
		/* power of 2 elements in the ring buffer, index is the lower n bits */
		wr_idx = ap->ap_wr_off & (ap->ap_ring_sz - 1);
		memcpy(ap->ap_buf + wr_idx * ap->ap_elem_sz, buf, ap->ap_elem_sz);
		ap->ap_wr_off++;
		buf += ap->ap_elem_sz;
		nr_copied++;
		if (__ring_full(ap->ap_ring_sz, ap->ap_wr_off, ap->ap_rd_off))
			break;
	}
	/* since we're using just one CV, there could be readers and writers blocked
	 * on it.  need to wake them all, to make sure we signal any blocked
	 * writers. */
	__cv_broadcast(&ap->ap_cv);
	cv_unlock(&ap->ap_cv);
	return nr_copied;
}

void *apipe_head(struct atomic_pipe *ap)
{
	if (__ring_empty(ap->ap_wr_off, ap->ap_rd_off))
		return 0;
	return ap->ap_buf + (ap->ap_rd_off & (ap->ap_ring_sz - 1)) * ap->ap_elem_sz;
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

	cv_lock(&ap->ap_cv);
	while (1){
		/* Each time there is a need to check the pipe, call
		 * f. f will maintain its state in arg. It is expected that f
		 * will dequeue elements from the pipe as they are available.
		 * N.B. this is being done for protocols like IPV4 that can
		 * fragment an RPC request. For IPV6, it is likely that this
		 * will end up looking like a blocking read. Thus was it ever
		 * with legacy code. F is supposed to call apipe_read_locked().
		 */
		/* at any point, if nr_writers goes to zero, that's bad. */
#if 0
		if (!ap->ap_nr_writers) {
			cv_unlock(&ap->ap_cv);
			/* return -1 because they're going to have to clean up. */
			return -1;
		}
#endif
		ret = f(ap, arg);
		if (ret)
			break;
		cv_wait(&ap->ap_cv);
		cpu_relax();
	}
	cv_unlock(&ap->ap_cv);
	return ret;
}

