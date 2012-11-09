/* Copyright (c) 2012 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Console (Keyboard/serial/whatever) related functions. */

#include <console.h>
#include <ros/ring_buffer.h>
#include <monitor.h>
#include <stdio.h>

struct kb_buffer cons_buf;

void kb_buf_init(struct kb_buffer *kb)
{
	kb->prod_idx = 0;
	kb->cons_idx = 0;
	spinlock_init(&kb->buf_lock);
	sem_init(&kb->buf_sem, 0);
	/* no need to memset the buffer - we only read something that is written */
}

/* Producers don't block - their input is dropped.  Should be rare for now; if
 * it happens, it's probably a bug. */
void kb_add_to_buf(struct kb_buffer *kb, char c)
{
	/* make sure we're a power of 2 */
	static_assert(KB_BUF_SIZE == __RD32(KB_BUF_SIZE));
	bool was_empty = FALSE;
	spin_lock(&kb->buf_lock);
	if (!__ring_full(KB_BUF_SIZE, kb->prod_idx, kb->cons_idx)) {
		if (__ring_empty(kb->prod_idx, kb->cons_idx))
			was_empty = TRUE;
		kb->buf[kb->prod_idx % KB_BUF_SIZE] = c;	// compiler should shift
		kb->prod_idx++;
	} else {
		/* else drop the char */
		printk("[kernel] KB buffer overflowed %c\n", c);
	}
	spin_unlock(&kb->buf_lock);
	/* up the sem when it goes from empty->non_empty.   rule for syncing with
	 * blockers: if there are any items in the buffer, either the sem is upped,
	 * or there is an active consumer.  consumers immediately down (to become an
	 * active consumer). */
	if (was_empty)
		sem_up(&kb->buf_sem);
	/* also note that multiple readers on the console/serial are going to fight
	 * for input and it is going to get interleaved - broader issue */
}

/* Will read cnt chars from the KB buf into dst.  Will block until complete. */
void kb_get_from_buf(struct kb_buffer *kb, char *dst, size_t cnt)
{
	unsigned int dst_idx = 0; /* aka, amt copied so far */
	bool need_an_up = FALSE;

	/* so long as we still need items, we'll sleep til there is activity, then
	 * grab everything we can til the kb buf is empty (or we're done).  If we
	 * didn't empty the buf, we'll need to up the sem later. */
	while (dst_idx < cnt) {
		/* this will return immediately if some data is already there, o/w we
		 * block til there is some activity */
		sem_down(&kb->buf_sem);
		spin_lock(&kb->buf_lock);
		/* under the current scheme, we should only have one active consumer at
		 * a time, so if we woke up, the ring must not be empty. */
		assert(!__ring_empty(kb->prod_idx, kb->cons_idx));
		while (!__ring_empty(kb->prod_idx, kb->cons_idx)) {
			if (dst_idx == cnt) {
				/* we're done, and it's not empty yet */
				need_an_up = TRUE;
				break;
			}
			dst[dst_idx] = kb->buf[kb->cons_idx % KB_BUF_SIZE];
			kb->cons_idx++;
			dst_idx++;
		}
		spin_unlock(&kb->buf_lock);
	}
	/* Remember: if the buf is non empty, there is either an active consumer or
	 * the sem is upped. */
	if (need_an_up)
		sem_up(&kb->buf_sem);
}

/* Kernel messages associated with the console.  Arch-specific interrupt
 * handlers need to call these.  For add char, a0 = &cons_buf and a1 = the char
 * you read.  Call __run_mon on your 'magic' input.  */
void __cons_add_char(struct trapframe *tf, uint32_t srcid, long a0, long a1,
                     long a2)
{
	kb_add_to_buf((struct kb_buffer*)a0, (char)a1);
}

void __run_mon(struct trapframe *tf, uint32_t srcid, long a0, long a1,
               long a2)
{
	monitor(0);
}
