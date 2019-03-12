/* Copyright (c) 2013 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Atomic pipes.  Multi-reader, multi-writer pipes, similar to sys_pipe except
 * that they operate on fixed sized chunks of data.
 *
 * Basic usage:
 *
 * Given:
 * 		struct some_struct {...};
 * 		struct atomic_pipe ap;
 * 		void *kpage_addr;
 *
 * 		apipe_init(&ap, kpage_addr, PGSIZE, sizeof(struct some_struct));
 * 		ret = apipe_write(&ap, &from_local_struct, 1);
 * 		...
 * 		ret = apipe_read(&ap, &to_local_struct, 1);
 * 		apipe_close_writer(&ap);
 * 		apipe_close_reader(&ap);
 *
 * Read and write return the number of elements copied.  If they copied any
 * amount, they will return.  They will block if the pipe is empty/full,
 * and there exist corresponding writers/readers.
 *
 * The only time the number of readers or writers matter is when the pipe
 * is empty or full.  I even allow writers to write, even if there are no
 * readers, so long as the pipe isn't full yet.  This allows new readers
 * to reattach, and pick up whatever was put in while there was no
 * readers.  If you don't plan to shut down the pipe, you can ignore the
 * readers/writers.
 *
 * Basically, this style prevents you from blocking if there is no one who
 * will ever wake you up.  In these cases (e.g. reader sees an empty pipe
 * and there are no writers), the read/write op returns 0, which means
 * "nothing to do, and can't block since you (possibly) won't wake up".
 * You might be able to try again in the future, but that's up to whatever
 * subsystem/code is using the pipes.
 *
 * I don't make any assumptions about the memory for the apipe.  It could be
 * kmalloced, embedded in a struct, whatever.  Hence the lack of refcnts too.
 *
 * Everything is multi-reader, multi-writer.  Pretty simple inside (no
 * fancy tricks, just went with a cv_lock for all ops).  If we want to
 * make this faster in the future, we can take a look at using some tricks
 * from the BCQs and Xen ring buffers to allow concurrent reads and writes.
 *
 * Likewise, we can make this a little more complicated and optimize for copying
 * many elements at once (like sys_pipe).  But we can hold off til we see how
 * people use this.  For now, this is built for one copy at a time. */

#pragma once

#include <ros/common.h>
#include <kthread.h>

struct atomic_pipe {
	char				*ap_buf;
	size_t				ap_ring_sz;
	size_t				ap_elem_sz;
	size_t				ap_rd_off;
	size_t				ap_wr_off;
	unsigned int			ap_nr_readers;
	unsigned int			ap_nr_writers;
	spinlock_t			ap_lock;
	struct cond_var			ap_priority_reader;
	struct cond_var			ap_general_readers;
	struct cond_var			ap_writers;
	bool				ap_has_priority_reader;
};

void apipe_init(struct atomic_pipe *ap, void *buf, size_t buf_sz,
                size_t elem_sz);
int apipe_read(struct atomic_pipe *ap, void *buf, size_t nr_elem);
int apipe_read_cond(struct atomic_pipe *ap,
		    int(*f)(struct atomic_pipe *pipe, void *arg), void *arg);
int apipe_write(struct atomic_pipe *ap, void *buf, size_t nr_elem);
void *apipe_head(struct atomic_pipe *ap);

void apipe_open_reader(struct atomic_pipe *ap);
void apipe_open_writer(struct atomic_pipe *ap);
void apipe_close_reader(struct atomic_pipe *ap);
void apipe_close_writer(struct atomic_pipe *ap);
