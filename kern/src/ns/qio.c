/* Copyright © 1994-1999 Lucent Technologies Inc.  All rights reserved.
 * Portions Copyright © 1997-1999 Vita Nuova Limited
 * Portions Copyright © 2000-2007 Vita Nuova Holdings Limited
 *                                (www.vitanuova.com)
 * Revisions Copyright © 2000-2007 Lucent Technologies Inc. and others
 *
 * Modified for the Akaros operating system:
 * Copyright (c) 2013-2014 The Regents of the University of California
 * Copyright (c) 2013-2015 Google Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE. */

#include <vfs.h>
#include <kfs.h>
#include <slab.h>
#include <kmalloc.h>
#include <kref.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <error.h>
#include <cpio.h>
#include <pmap.h>
#include <smp.h>
#include <ip.h>

#define PANIC_EXTRA(b)							\
{									\
	if ((b)->extra_len) {						\
		printblock(b);						\
		backtrace();						\
		panic("%s doesn't handle extra_data", __FUNCTION__);	\
	}								\
}

static uint32_t padblockcnt;
static uint32_t concatblockcnt;
static uint32_t pullupblockcnt;
static uint32_t copyblockcnt;
static uint32_t consumecnt;
static uint32_t producecnt;
static uint32_t qcopycnt;

static int debugging;

#define QDEBUG	if(0)

/*
 *  IO queues
 */

struct queue {
	spinlock_t lock;;

	struct block *bfirst;		/* buffer */
	struct block *blast;

	int dlen;					/* data bytes in queue */
	int limit;					/* max bytes in queue */
	int inilim;				/* initial limit */
	int state;
	int eof;					/* number of eofs read by user */

	void (*kick) (void *);		/* restart output */
	void (*bypass) (void *, struct block *);	/* bypass queue altogether */
	void *arg;					/* argument to kick */

	struct rendez rr;			/* process waiting to read */
	struct rendez wr;			/* process waiting to write */
	qio_wake_cb_t wake_cb;		/* callbacks for qio wakeups */
	void *wake_data;

	char err[ERRMAX];
};

enum {
	Maxatomic = 64 * 1024,
	QIO_CAN_ERR_SLEEP = (1 << 0),	/* can throw errors or block/sleep */
	QIO_LIMIT = (1 << 1),			/* respect q->limit */
	QIO_DROP_OVERFLOW = (1 << 2),	/* alternative to setting qdropoverflow */
	QIO_JUST_ONE_BLOCK = (1 << 3),	/* when qbreading, just get one block */
	QIO_NON_BLOCK = (1 << 4),		/* throw EAGAIN instead of blocking */
	QIO_DONT_KICK = (1 << 5),		/* don't kick when waking */
};

unsigned int qiomaxatomic = Maxatomic;

static size_t copy_to_block_body(struct block *to, void *from, size_t copy_amt);
static ssize_t __qbwrite(struct queue *q, struct block *b, int flags);
static struct block *__qbread(struct queue *q, size_t len, int qio_flags,
                              int mem_flags);
static bool qwait_and_ilock(struct queue *q, int qio_flags);

/* Helper: fires a wake callback, sending 'filter' */
static void qwake_cb(struct queue *q, int filter)
{
	if (q->wake_cb)
		q->wake_cb(q, q->wake_data, filter);
}

void ixsummary(void)
{
	debugging ^= 1;
	printd("pad %lu, concat %lu, pullup %lu, copy %lu\n",
		   padblockcnt, concatblockcnt, pullupblockcnt, copyblockcnt);
	printd("consume %lu, produce %lu, qcopy %lu\n",
		   consumecnt, producecnt, qcopycnt);
}

/*
 *  pad a block to the front (or the back if size is negative)
 */
struct block *padblock(struct block *bp, int size)
{
	int n;
	struct block *nbp;
	uint8_t bcksum = bp->flag & BCKSUM_FLAGS;
	uint16_t checksum_start = bp->checksum_start;
	uint16_t checksum_offset = bp->checksum_offset;
	uint16_t mss = bp->mss;

	QDEBUG checkb(bp, "padblock 1");
	if (size >= 0) {
		if (bp->rp - bp->base >= size) {
			bp->checksum_start += size;
			bp->rp -= size;
			return bp;
		}

		PANIC_EXTRA(bp);
		if (bp->next)
			panic("padblock %p", getcallerpc(&bp));
		n = BLEN(bp);
		padblockcnt++;
		nbp = block_alloc(size + n, MEM_WAIT);
		nbp->rp += size;
		nbp->wp = nbp->rp;
		memmove(nbp->wp, bp->rp, n);
		nbp->wp += n;
		freeb(bp);
		nbp->rp -= size;
	} else {
		size = -size;

		PANIC_EXTRA(bp);

		if (bp->next)
			panic("padblock %p", getcallerpc(&bp));

		if (bp->lim - bp->wp >= size)
			return bp;

		n = BLEN(bp);
		padblockcnt++;
		nbp = block_alloc(size + n, MEM_WAIT);
		memmove(nbp->wp, bp->rp, n);
		nbp->wp += n;
		freeb(bp);
	}
	if (bcksum) {
		nbp->flag |= bcksum;
		nbp->checksum_start = checksum_start;
		nbp->checksum_offset = checksum_offset;
		nbp->mss = mss;
	}
	QDEBUG checkb(nbp, "padblock 1");
	return nbp;
}

/*
 *  return count of bytes in a string of blocks
 */
int blocklen(struct block *bp)
{
	int len;

	len = 0;
	while (bp) {
		len += BLEN(bp);
		bp = bp->next;
	}
	return len;
}

/*
 * return count of space in blocks
 */
int blockalloclen(struct block *bp)
{
	int len;

	len = 0;
	while (bp) {
		len += BALLOC(bp);
		bp = bp->next;
	}
	return len;
}

/*
 *  copy the  string of blocks into
 *  a single block and free the string
 */
struct block *concatblock(struct block *bp)
{
	int len;
	struct block *nb, *f;

	if (bp->next == 0)
		return bp;

	/* probably use parts of qclone */
	PANIC_EXTRA(bp);
	nb = block_alloc(blocklen(bp), MEM_WAIT);
	for (f = bp; f; f = f->next) {
		len = BLEN(f);
		memmove(nb->wp, f->rp, len);
		nb->wp += len;
	}
	concatblockcnt += BLEN(nb);
	freeblist(bp);
	QDEBUG checkb(nb, "concatblock 1");
	return nb;
}

/* Makes an identical copy of the block, collapsing all the data into the block
 * body.  It does not point to the contents of the original, it is a copy
 * (unlike qclone).  Since we're copying, we might as well put the memory into
 * one contiguous chunk. */
struct block *copyblock(struct block *bp, int mem_flags)
{
	struct block *newb;
	struct extra_bdata *ebd;
	size_t amt;

	QDEBUG checkb(bp, "copyblock 0");
	newb = block_alloc(BLEN(bp), mem_flags);
	if (!newb)
		return 0;
	amt = copy_to_block_body(newb, bp->rp, BHLEN(bp));
	assert(amt == BHLEN(bp));
	for (int i = 0; i < bp->nr_extra_bufs; i++) {
		ebd = &bp->extra_data[i];
		if (!ebd->base || !ebd->len)
			continue;
		amt = copy_to_block_body(newb, (void*)ebd->base + ebd->off, ebd->len);
		assert(amt == ebd->len);
	}
	/* TODO: any other flags that need copied over? */
	if (bp->flag & BCKSUM_FLAGS) {
		newb->flag |= (bp->flag & BCKSUM_FLAGS);
		newb->checksum_start = bp->checksum_start;
		newb->checksum_offset = bp->checksum_offset;
		newb->mss = bp->mss;
	}
	copyblockcnt++;
	QDEBUG checkb(newb, "copyblock 1");
	return newb;
}

/* Returns a block with the remaining contents of b all in the main body of the
 * returned block.  Replace old references to b with the returned value (which
 * may still be 'b', if no change was needed. */
struct block *linearizeblock(struct block *b)
{
	struct block *newb;

	if (!b->extra_len)
		return b;
	newb = copyblock(b, MEM_WAIT);
	freeb(b);
	return newb;
}

/* Make sure the first block has at least n bytes in its main body.  Pulls up
 * data from the *list* of blocks.  Returns 0 if there is not enough data in the
 * block list. */
struct block *pullupblock(struct block *bp, int n)
{
	int i, len, seglen;
	struct block *nbp;
	struct extra_bdata *ebd;

	/*
	 *  this should almost always be true, it's
	 *  just to avoid every caller checking.
	 */
	if (BHLEN(bp) >= n)
		return bp;

	/* If there's no chance, just bail out now.  This might be slightly wasteful
	 * if there's a long blist that does have enough data. */
	if (n > blocklen(bp))
		return 0;
	/* a start at explicit main-body / header management */
	if (bp->extra_len) {
		if (n > bp->lim - bp->rp) {
			/* would need to realloc a new block and copy everything over. */
			panic("can't pullup %d bytes, no place to put it: bp->lim %p, bp->rp %p, bp->lim-bp->rp %d\n",
					n, bp->lim, bp->rp, bp->lim-bp->rp);
		}
		len = n - BHLEN(bp);
		/* Would need to recursively call this, or otherwise pull from later
		 * blocks and put chunks of their data into the block we're building. */
		if (len > bp->extra_len)
			panic("pullup more than extra (%d, %d, %d)\n",
			      n, BHLEN(bp), bp->extra_len);
		QDEBUG checkb(bp, "before pullup");
		for (int i = 0; (i < bp->nr_extra_bufs) && len; i++) {
			ebd = &bp->extra_data[i];
			if (!ebd->base || !ebd->len)
				continue;
			seglen = MIN(ebd->len, len);
			memcpy(bp->wp, (void*)(ebd->base + ebd->off), seglen);
			bp->wp += seglen;
			len -= seglen;
			ebd->len -= seglen;
			ebd->off += seglen;
			bp->extra_len -= seglen;
			if (ebd->len == 0) {
				kfree((void *)ebd->base);
				ebd->off = 0;
				ebd->base = 0;
			}
		}
		/* maybe just call pullupblock recursively here */
		if (len)
			panic("pullup %d bytes overdrawn\n", len);
		QDEBUG checkb(bp, "after pullup");
		return bp;
	}

	/*
	 *  if not enough room in the first block,
	 *  add another to the front of the list.
	 */
	if (bp->lim - bp->rp < n) {
		nbp = block_alloc(n, MEM_WAIT);
		nbp->next = bp;
		bp = nbp;
	}

	/*
	 *  copy bytes from the trailing blocks into the first
	 */
	n -= BLEN(bp);
	while ((nbp = bp->next)) {
		i = BLEN(nbp);
		if (i > n) {
			memmove(bp->wp, nbp->rp, n);
			pullupblockcnt++;
			bp->wp += n;
			nbp->rp += n;
			QDEBUG checkb(bp, "pullupblock 1");
			return bp;
		} else {
			memmove(bp->wp, nbp->rp, i);
			pullupblockcnt++;
			bp->wp += i;
			bp->next = nbp->next;
			nbp->next = 0;
			freeb(nbp);
			n -= i;
			if (n == 0) {
				QDEBUG checkb(bp, "pullupblock 2");
				return bp;
			}
		}
	}
	freeb(bp);
	return 0;
}

/*
 *  make sure the first block has at least n bytes in its main body
 */
struct block *pullupqueue(struct queue *q, int n)
{
	struct block *b;

	/* TODO: lock to protect the queue links? */
	if ((BHLEN(q->bfirst) >= n))
		return q->bfirst;
	q->bfirst = pullupblock(q->bfirst, n);
	for (b = q->bfirst; b != NULL && b->next != NULL; b = b->next) ;
	q->blast = b;
	return q->bfirst;
}

/* throw away count bytes from the front of
 * block's extradata.  Returns count of bytes
 * thrown away
 */

static int pullext(struct block *bp, int count)
{
	struct extra_bdata *ed;
	int i, rem, bytes = 0;

	for (i = 0; bp->extra_len && count && i < bp->nr_extra_bufs; i++) {
		ed = &bp->extra_data[i];
		rem = MIN(count, ed->len);
		bp->extra_len -= rem;
		count -= rem;
		bytes += rem;
		ed->off += rem;
		ed->len -= rem;
		if (ed->len == 0) {
			kfree((void *)ed->base);
			ed->base = 0;
			ed->off = 0;
		}
	}
	return bytes;
}

/* throw away count bytes from the end of a
 * block's extradata.  Returns count of bytes
 * thrown away
 */

static int dropext(struct block *bp, int count)
{
	struct extra_bdata *ed;
	int i, rem, bytes = 0;

	for (i = bp->nr_extra_bufs - 1; bp->extra_len && count && i >= 0; i--) {
		ed = &bp->extra_data[i];
		rem = MIN(count, ed->len);
		bp->extra_len -= rem;
		count -= rem;
		bytes += rem;
		ed->len -= rem;
		if (ed->len == 0) {
			kfree((void *)ed->base);
			ed->base = 0;
			ed->off = 0;
		}
	}
	return bytes;
}

/*
 *  throw away up to count bytes from a
 *  list of blocks.  Return count of bytes
 *  thrown away.
 */
static int _pullblock(struct block **bph, int count, int free)
{
	struct block *bp;
	int n, bytes;

	bytes = 0;
	if (bph == NULL)
		return 0;

	while (*bph != NULL && count != 0) {
		bp = *bph;

		n = MIN(BHLEN(bp), count);
		bytes += n;
		count -= n;
		bp->rp += n;
		n = pullext(bp, count);
		bytes += n;
		count -= n;
		QDEBUG checkb(bp, "pullblock ");
		if (BLEN(bp) == 0 && (free || count)) {
			*bph = bp->next;
			bp->next = NULL;
			freeb(bp);
		}
	}
	return bytes;
}

int pullblock(struct block **bph, int count)
{
	return _pullblock(bph, count, 1);
}

/*
 *  trim to len bytes starting at offset
 */
struct block *trimblock(struct block *bp, int offset, int len)
{
	uint32_t l, trim;
	int olen = len;

	QDEBUG checkb(bp, "trimblock 1");
	if (blocklen(bp) < offset + len) {
		freeblist(bp);
		return NULL;
	}

	l =_pullblock(&bp, offset, 0);
	if (bp == NULL)
		return NULL;
	if (l != offset) {
		freeblist(bp);
		return NULL;
	}

	while ((l = BLEN(bp)) < len) {
		len -= l;
		bp = bp->next;
	}

	trim = BLEN(bp) - len;
	trim -= dropext(bp, trim);
	bp->wp -= trim;

	if (bp->next) {
		freeblist(bp->next);
		bp->next = NULL;
	}
	return bp;
}

/* Adjust block @bp so that its size is exactly @len.
 * If the size is increased, fill in the new contents with zeros.
 * If the size is decreased, discard some of the old contents at the tail. */
struct block *adjustblock(struct block *bp, int len)
{
	struct extra_bdata *ebd;
	void *buf;
	int i;

	if (len < 0) {
		freeb(bp);
		return NULL;
	}

	if (len == BLEN(bp))
		return bp;

	/* Shrink within block main body. */
	if (len <= BHLEN(bp)) {
		free_block_extra(bp);
		bp->wp = bp->rp + len;
		QDEBUG checkb(bp, "adjustblock 1");
		return bp;
	}

	/* Need to grow. */
	if (len > BLEN(bp)) {
		/* Grow within block main body. */
		if (bp->extra_len == 0 && bp->rp + len <= bp->lim) {
			memset(bp->wp, 0, len - BLEN(bp));
			bp->wp = bp->rp + len;
			QDEBUG checkb(bp, "adjustblock 2");
			return bp;
		}
		/* Grow with extra data buffers. */
		buf = kzmalloc(len - BLEN(bp), MEM_WAIT);
		block_append_extra(bp, (uintptr_t)buf, 0, len - BLEN(bp), MEM_WAIT);
		QDEBUG checkb(bp, "adjustblock 3");
		return bp;
	}

	/* Shrink extra data buffers.
	 * len is how much of ebd we need to keep.
	 * extra_len is re-accumulated. */
	assert(bp->extra_len > 0);
	len -= BHLEN(bp);
	bp->extra_len = 0;
	for (i = 0; i < bp->nr_extra_bufs; i++) {
		ebd = &bp->extra_data[i];
		if (len <= ebd->len)
			break;
		len -= ebd->len;
		bp->extra_len += ebd->len;
	}
	/* If len becomes zero, extra_data[i] should be freed. */
	if (len > 0) {
		ebd = &bp->extra_data[i];
		ebd->len = len;
		bp->extra_len += ebd->len;
		i++;
	}
	for (; i < bp->nr_extra_bufs; i++) {
		ebd = &bp->extra_data[i];
		if (ebd->base)
			kfree((void*)ebd->base);
		ebd->base = ebd->off = ebd->len = 0;
	}
	QDEBUG checkb(bp, "adjustblock 4");
	return bp;
}

/* Helper: removes and returns the first block from q */
static struct block *pop_first_block(struct queue *q)
{
	struct block *b = q->bfirst;

	q->dlen -= BLEN(b);
	q->bfirst = b->next;
	b->next = 0;
	return b;
}

/* Helper: copies up to copy_amt from a buf to a block's main body (b->wp) */
static size_t copy_to_block_body(struct block *to, void *from, size_t copy_amt)
{
	copy_amt = MIN(to->lim - to->wp, copy_amt);
	memcpy(to->wp, from, copy_amt);
	to->wp += copy_amt;
	return copy_amt;
}

/* Accounting helper.  Block b in q lost amt extra_data */
static void block_and_q_lost_extra(struct block *b, struct queue *q, size_t amt)
{
	b->extra_len -= amt;
	q->dlen -= amt;
}

/* Helper: moves ebd from a block (in from_q) to another block.  The *ebd is
 * fixed in 'from', so we move its contents and zero it out in 'from'.
 *
 * Returns the length moved (0 on failure). */
static size_t move_ebd(struct extra_bdata *ebd, struct block *to,
                       struct block *from, struct queue *from_q)
{
	size_t ret = ebd->len;

	if (block_append_extra(to, ebd->base, ebd->off, ebd->len, MEM_ATOMIC))
		return 0;
	block_and_q_lost_extra(from, from_q, ebd->len);
	ebd->base = ebd->len = ebd->off = 0;
	return ret;
}

/* Copy up to len bytes from q->bfirst to @to, leaving the block in place.  May
 * return with less than len, but greater than 0, even if there is more
 * available in q.
 *
 * At any moment that we have copied anything and things are tricky, we can just
 * return.  The trickiness comes from a bunch of variables: is the main body
 * empty?  How do we split the ebd?  If our alloc fails, then we can fall back
 * to @to's main body, but only if we haven't used it yet. */
static size_t copy_from_first_block(struct queue *q, struct block *to,
                                    size_t len)
{
	struct block *from = q->bfirst;
	size_t copy_amt, amt;
	struct extra_bdata *ebd;

	assert(len < BLEN(from));	/* sanity */
	/* Try to extract from the main body */
	copy_amt = MIN(BHLEN(from), len);
	if (copy_amt) {
		copy_amt = copy_to_block_body(to, from->rp, copy_amt);
		from->rp += copy_amt;
		/* We only change dlen, (data len), not q->len, since the q still has
		 * the same block memory allocation (no kfrees happened) */
		q->dlen -= copy_amt;
	}
	/* Try to extract the remainder from the extra data */
	len -= copy_amt;
	for (int i = 0; (i < from->nr_extra_bufs) && len; i++) {
		ebd = &from->extra_data[i];
		if (!ebd->base || !ebd->len)
			continue;
		if (len >= ebd->len) {
			amt = move_ebd(ebd, to, from, q);
			if (!amt) {
				/* our internal alloc could have failed.   this ebd is now the
				 * last one we'll consider.  let's handle it separately and put
				 * it in the main body. */
				if (copy_amt)
					return copy_amt;
				copy_amt = copy_to_block_body(to, (void*)ebd->base + ebd->off,
				                              ebd->len);
				block_and_q_lost_extra(from, q, copy_amt);
				break;
			}
			len -= amt;
			copy_amt += amt;
			continue;
		} else {
			/* If we're here, we reached our final ebd, which we'll need to
			 * split to get anything from it. */
			if (copy_amt)
				return copy_amt;
			copy_amt = copy_to_block_body(to, (void*)ebd->base + ebd->off,
			                              len);
			ebd->off += copy_amt;
			ebd->len -= copy_amt;
			block_and_q_lost_extra(from, q, copy_amt);
			break;
		}
	}
	if (len)
		assert(copy_amt);	/* sanity */
	return copy_amt;
}

/* Return codes for __qbread and __try_qbread. */
enum {
	QBR_OK,
	QBR_FAIL,
	QBR_SPARE,	/* we need a spare block */
	QBR_AGAIN,	/* do it again, we are coalescing blocks */
};

/* Helper and back-end for __qbread: extracts and returns a list of blocks
 * containing up to len bytes.  It may contain less than len even if q has more
 * data.
 *
 * Returns a code interpreted by __qbread, and the returned blist in ret. */
static int __try_qbread(struct queue *q, size_t len, int qio_flags,
                        struct block **real_ret, struct block *spare)
{
	struct block *ret, *ret_last, *first;
	size_t blen;
	bool was_unwritable = FALSE;

	if (qio_flags & QIO_CAN_ERR_SLEEP) {
		if (!qwait_and_ilock(q, qio_flags)) {
			spin_unlock_irqsave(&q->lock);
			return QBR_FAIL;
		}
		/* we qwaited and still hold the lock, so the q is not empty */
		first = q->bfirst;
	} else {
		spin_lock_irqsave(&q->lock);
		first = q->bfirst;
		if (!first) {
			spin_unlock_irqsave(&q->lock);
			return QBR_FAIL;
		}
	}
	/* We need to check before adjusting q->len.  We're checking the writer's
	 * sleep condition / tap condition.  When set, we *might* be making an edge
	 * transition (from unwritable to writable), which needs to wake and fire
	 * taps.  But, our read might not drain the queue below q->lim.  We'll check
	 * again later to see if we should really wake them.  */
	was_unwritable = !qwritable(q);
	blen = BLEN(first);
	if ((q->state & Qcoalesce) && (blen == 0)) {
		freeb(pop_first_block(q));
		spin_unlock_irqsave(&q->lock);
		/* Need to retry to make sure we have a first block */
		return QBR_AGAIN;
	}
	/* Qmsg: just return the first block.  Be careful, since our caller might
	 * not read all of the block and thus drop bytes.  Similar to SOCK_DGRAM. */
	if (q->state & Qmsg) {
		ret = pop_first_block(q);
		goto out_ok;
	}
	/* Let's get at least something first - makes the code easier.  This way,
	 * we'll only ever split the block once. */
	if (blen <= len) {
		ret = pop_first_block(q);
		len -= blen;
	} else {
		/* need to split the block.  we won't actually take the first block out
		 * of the queue - we're just extracting a little bit. */
		if (!spare) {
			/* We have nothing and need a spare block.  Retry! */
			spin_unlock_irqsave(&q->lock);
			return QBR_SPARE;
		}
		copy_from_first_block(q, spare, len);
		ret = spare;
		goto out_ok;
	}
	/* At this point, we just grabbed the first block.  We can try to grab some
	 * more, up to len (if they want). */
	if (qio_flags & QIO_JUST_ONE_BLOCK)
		goto out_ok;
	ret_last = ret;
	while (q->bfirst && (len > 0)) {
		blen = BLEN(q->bfirst);
		if ((q->state & Qcoalesce) && (blen == 0)) {
			/* remove the intermediate 0 blocks */
			freeb(pop_first_block(q));
			continue;
		}
		if (blen > len) {
			/* We could try to split the block, but that's a huge pain.  For
			 * instance, we might need to move the main body of b into an
			 * extra_data of ret_last.  lots of ways for that to fail, and lots
			 * of cases to consider.  Easier to just bail out.  This is why I
			 * did the first block above: we don't need to worry about this. */
			 break;
		}
		ret_last->next = pop_first_block(q);
		ret_last = ret_last->next;
		len -= blen;
	}
out_ok:
	/* Don't wake them up or fire tap if we didn't drain enough. */
	if (!qwritable(q))
		was_unwritable = FALSE;
	spin_unlock_irqsave(&q->lock);
	if (was_unwritable) {
		if (q->kick && !(qio_flags & QIO_DONT_KICK))
			q->kick(q->arg);
		rendez_wakeup(&q->wr);
		qwake_cb(q, FDTAP_FILT_WRITABLE);
	}
	*real_ret = ret;
	return QBR_OK;
}

/* Helper and front-end for __try_qbread: extracts and returns a list of blocks
 * containing up to len bytes.  It may contain less than len even if q has more
 * data.
 *
 * Returns 0 if the q is closed or would require blocking and !CAN_BLOCK.
 *
 * Technically, there's a weird corner case with !Qcoalesce and Qmsg where you
 * could get a zero length block back. */
static struct block *__qbread(struct queue *q, size_t len, int qio_flags,
                              int mem_flags)
{
	struct block *ret = 0;
	struct block *spare = 0;

	while (1) {
		switch (__try_qbread(q, len, qio_flags, &ret, spare)) {
		case QBR_OK:
		case QBR_FAIL:
			if (spare && (ret != spare))
				freeb(spare);
			return ret;
		case QBR_SPARE:
			assert(!spare);
			/* Due to some nastiness, we need a fresh block so we can read out
			 * anything from the queue.  'len' seems like a reasonable amount.
			 * Maybe we can get away with less. */
			spare = block_alloc(len, mem_flags);
			if (!spare)
				return 0;
			break;
		case QBR_AGAIN:
			/* if the first block is 0 and we are Qcoalesce, then we'll need to
			 * try again.  We bounce out of __try so we can perform the "is
			 * there a block" logic again from the top. */
			break;
		}
	}
}

/*
 *  get next block from a queue, return null if nothing there
 */
struct block *qget(struct queue *q)
{
	/* since len == SIZE_MAX, we should never need to do a mem alloc */
	return __qbread(q, SIZE_MAX, QIO_JUST_ONE_BLOCK, MEM_ATOMIC);
}

/* Throw away the next 'len' bytes in the queue returning the number actually
 * discarded.
 *
 * If the bytes are in the queue, then they must be discarded.  The only time to
 * return less than len is if the q itself has less than len bytes.
 *
 * This won't trigger a kick when waking up any sleepers.  This seems to be Plan
 * 9's intent, since the TCP stack will deadlock if qdiscard kicks. */
size_t qdiscard(struct queue *q, size_t len)
{
	struct block *blist;
	size_t removed_amt;
	size_t sofar = 0;

	/* This is racy.  There could be multiple qdiscarders or other consumers,
	 * where the consumption could be interleaved. */
	while (qlen(q) && len) {
		blist = __qbread(q, len, QIO_DONT_KICK, MEM_WAIT);
		removed_amt = freeblist(blist);
		sofar += removed_amt;
		len -= removed_amt;
	}
	return sofar;
}

ssize_t qpass(struct queue *q, struct block *b)
{
	return __qbwrite(q, b, QIO_LIMIT | QIO_DROP_OVERFLOW);
}

ssize_t qpassnolim(struct queue *q, struct block *b)
{
	return __qbwrite(q, b, 0);
}

/*
 *  if the allocated space is way out of line with the used
 *  space, reallocate to a smaller block
 */
struct block *packblock(struct block *bp)
{
	struct block **l, *nbp;
	int n;

	if (bp->extra_len)
		return bp;
	for (l = &bp; *l; l = &(*l)->next) {
		nbp = *l;
		n = BLEN(nbp);
		if ((n << 2) < BALLOC(nbp)) {
			*l = block_alloc(n, MEM_WAIT);
			memmove((*l)->wp, nbp->rp, n);
			(*l)->wp += n;
			(*l)->next = nbp->next;
			freeb(nbp);
		}
	}

	return bp;
}

/* Add an extra_data entry to newb at newb_idx pointing to b's body, starting at
 * body_rp, for up to len.  Returns the len consumed.
 *
 * The base is 'b', so that we can kfree it later.  This currently ties us to
 * using kfree for the release method for all extra_data.
 *
 * It is possible to have a body size that is 0, if there is no offset, and
 * b->wp == b->rp.  This will have an extra data entry of 0 length. */
static size_t point_to_body(struct block *b, uint8_t *body_rp,
                            struct block *newb, unsigned int newb_idx,
                            size_t len)
{
	struct extra_bdata *ebd = &newb->extra_data[newb_idx];

	assert(newb_idx < newb->nr_extra_bufs);

	kmalloc_incref(b);
	ebd->base = (uintptr_t)b;
	ebd->off = (uint32_t)(body_rp - (uint8_t*)b);
	ebd->len = MIN(b->wp - body_rp, len);	/* think of body_rp as b->rp */
	assert((int)ebd->len >= 0);
	newb->extra_len += ebd->len;
	return ebd->len;
}

/* Add an extra_data entry to newb at newb_idx pointing to b's b_idx'th
 * extra_data buf, at b_off within that buffer, for up to len.  Returns the len
 * consumed.
 *
 * We can have blocks with 0 length, but they are still refcnt'd.  See above. */
static size_t point_to_buf(struct block *b, unsigned int b_idx, uint32_t b_off,
                           struct block *newb, unsigned int newb_idx,
                           size_t len)
{
	struct extra_bdata *n_ebd = &newb->extra_data[newb_idx];
	struct extra_bdata *b_ebd = &b->extra_data[b_idx];

	assert(b_idx < b->nr_extra_bufs);
	assert(newb_idx < newb->nr_extra_bufs);

	kmalloc_incref((void*)b_ebd->base);
	n_ebd->base = b_ebd->base;
	n_ebd->off = b_ebd->off + b_off;
	n_ebd->len = MIN(b_ebd->len - b_off, len);
	newb->extra_len += n_ebd->len;
	return n_ebd->len;
}

/* given a string of blocks, sets up the new block's extra_data such that it
 * *points* to the contents of the blist [offset, len + offset).  This does not
 * make a separate copy of the contents of the blist.
 *
 * returns 0 on success.  the only failure is if the extra_data array was too
 * small, so this returns a positive integer saying how big the extra_data needs
 * to be.
 *
 * callers are responsible for protecting the list structure. */
static int __blist_clone_to(struct block *blist, struct block *newb, int len,
                            uint32_t offset)
{
	struct block *b, *first;
	unsigned int nr_bufs = 0;
	unsigned int b_idx, newb_idx = 0;
	uint8_t *first_main_body = 0;

	/* find the first block; keep offset relative to the latest b in the list */
	for (b = blist; b; b = b->next) {
		if (BLEN(b) > offset)
			break;
		offset -= BLEN(b);
	}
	/* qcopy semantics: if you asked for an offset outside the block list, you
	 * get an empty block back */
	if (!b)
		return 0;
	first = b;
	/* upper bound for how many buffers we'll need in newb */
	for (/* b is set*/; b; b = b->next) {
		nr_bufs += 1 + b->nr_extra_bufs;	/* 1 for the main body */
	}
	/* we might be holding a spinlock here, so we won't wait for kmalloc */
	if (block_add_extd(newb, nr_bufs, 0) != 0) {
		/* caller will need to alloc these, then re-call us */
		return nr_bufs;
	}
	for (b = first; b && len; b = b->next) {
		b_idx = 0;
		if (offset) {
			if (offset < BHLEN(b)) {
				/* off is in the main body */
				len -= point_to_body(b, b->rp + offset, newb, newb_idx, len);
				newb_idx++;
			} else {
				/* off is in one of the buffers (or just past the last one).
				 * we're not going to point to b's main body at all. */
				offset -= BHLEN(b);
				assert(b->extra_data);
				/* assuming these extrabufs are packed, or at least that len
				 * isn't gibberish */
				while (b->extra_data[b_idx].len <= offset) {
					offset -= b->extra_data[b_idx].len;
					b_idx++;
				}
				/* now offset is set to our offset in the b_idx'th buf */
				len -= point_to_buf(b, b_idx, offset, newb, newb_idx, len);
				newb_idx++;
				b_idx++;
			}
			offset = 0;
		} else {
			len -= point_to_body(b, b->rp, newb, newb_idx, len);
			newb_idx++;
		}
		/* knock out all remaining bufs.  we only did one point_to_ op by now,
		 * and any point_to_ could be our last if it consumed all of len. */
		for (int i = b_idx; (i < b->nr_extra_bufs) && len; i++) {
			len -= point_to_buf(b, i, 0, newb, newb_idx, len);
			newb_idx++;
		}
	}
	return 0;
}

struct block *blist_clone(struct block *blist, int header_len, int len,
                          uint32_t offset)
{
	int ret;
	struct block *newb = block_alloc(header_len, MEM_WAIT);
	do {
		ret = __blist_clone_to(blist, newb, len, offset);
		if (ret)
			block_add_extd(newb, ret, MEM_WAIT);
	} while (ret);
	return newb;
}

/* given a queue, makes a single block with header_len reserved space in the
 * block main body, and the contents of [offset, len + offset) pointed to in the
 * new blocks ext_data.  This does not make a copy of the q's contents, though
 * you do have a ref count on the memory. */
struct block *qclone(struct queue *q, int header_len, int len, uint32_t offset)
{
	int ret;
	struct block *newb = block_alloc(header_len, MEM_WAIT);
	/* the while loop should rarely be used: it would require someone
	 * concurrently adding to the queue. */
	do {
		/* TODO: RCU: protecting the q list (b->next) (need read lock) */
		spin_lock_irqsave(&q->lock);
		ret = __blist_clone_to(q->bfirst, newb, len, offset);
		spin_unlock_irqsave(&q->lock);
		if (ret)
			block_add_extd(newb, ret, MEM_WAIT);
	} while (ret);
	return newb;
}

/*
 *  copy from offset in the queue
 */
struct block *qcopy_old(struct queue *q, int len, uint32_t offset)
{
	int sofar;
	int n;
	struct block *b, *nb;
	uint8_t *p;

	nb = block_alloc(len, MEM_WAIT);

	spin_lock_irqsave(&q->lock);

	/* go to offset */
	b = q->bfirst;
	for (sofar = 0;; sofar += n) {
		if (b == NULL) {
			spin_unlock_irqsave(&q->lock);
			return nb;
		}
		n = BLEN(b);
		if (sofar + n > offset) {
			p = b->rp + offset - sofar;
			n -= offset - sofar;
			break;
		}
		QDEBUG checkb(b, "qcopy");
		b = b->next;
	}

	/* copy bytes from there */
	for (sofar = 0; sofar < len;) {
		if (n > len - sofar)
			n = len - sofar;
		PANIC_EXTRA(b);
		memmove(nb->wp, p, n);
		qcopycnt += n;
		sofar += n;
		nb->wp += n;
		b = b->next;
		if (b == NULL)
			break;
		n = BLEN(b);
		p = b->rp;
	}
	spin_unlock_irqsave(&q->lock);

	return nb;
}

struct block *qcopy(struct queue *q, int len, uint32_t offset)
{
#ifdef CONFIG_BLOCK_EXTRAS
	return qclone(q, 0, len, offset);
#else
	return qcopy_old(q, len, offset);
#endif
}

static void qinit_common(struct queue *q)
{
	spinlock_init_irqsave(&q->lock);
	rendez_init(&q->rr);
	rendez_init(&q->wr);
}

/*
 *  called by non-interrupt code
 */
struct queue *qopen(int limit, int msg, void (*kick) (void *), void *arg)
{
	struct queue *q;

	q = kzmalloc(sizeof(struct queue), 0);
	if (q == 0)
		return 0;
	qinit_common(q);

	q->limit = q->inilim = limit;
	q->kick = kick;
	q->arg = arg;
	q->state = msg;
	q->eof = 0;

	return q;
}

/* open a queue to be bypassed */
struct queue *qbypass(void (*bypass) (void *, struct block *), void *arg)
{
	struct queue *q;

	q = kzmalloc(sizeof(struct queue), 0);
	if (q == 0)
		return 0;
	qinit_common(q);

	q->limit = 0;
	q->arg = arg;
	q->bypass = bypass;
	q->state = 0;

	return q;
}

static int notempty(void *a)
{
	struct queue *q = a;

	return (q->state & Qclosed) || q->bfirst != 0;
}

/* Block, waiting for the queue to be non-empty or closed.  Returns with
 * the spinlock held.  Returns TRUE when there queue is not empty, FALSE if it
 * was naturally closed.  Throws an error o/w. */
static bool qwait_and_ilock(struct queue *q, int qio_flags)
{
	while (1) {
		spin_lock_irqsave(&q->lock);
		if (q->bfirst != NULL)
			return TRUE;
		if (q->state & Qclosed) {
			if (++q->eof > 3) {
				spin_unlock_irqsave(&q->lock);
				error(EPIPE, "multiple reads on a closed queue");
			}
			if (q->err[0]) {
				spin_unlock_irqsave(&q->lock);
				error(EPIPE, q->err);
			}
			return FALSE;
		}
		if (qio_flags & QIO_NON_BLOCK) {
			spin_unlock_irqsave(&q->lock);
			error(EAGAIN, "queue empty");
		}
		spin_unlock_irqsave(&q->lock);
		/* As with the producer side, we check for a condition while holding the
		 * q->lock, decide to sleep, then unlock.  It's like the "check, signal,
		 * check again" pattern, but we do it conditionally.  Both sides agree
		 * synchronously to do it, and those decisions are made while holding
		 * q->lock.  I think this is OK.
		 *
		 * The invariant is that no reader sleeps when the queue has data.
		 * While holding the rendez lock, if we see there's no data, we'll
		 * sleep.  Since we saw there was no data, the next writer will see (or
		 * already saw) no data, and then the writer decides to rendez_wake,
		 * which will grab the rendez lock.  If the writer already did that,
		 * then we'll see notempty when we do our check-again. */
		rendez_sleep(&q->rr, notempty, q);
	}
}

/*
 * add a block list to a queue
 * XXX basically the same as enqueue blist, and has no locking!
 */
void qaddlist(struct queue *q, struct block *b)
{
	/* TODO: q lock? */
	/* queue the block */
	if (q->bfirst)
		q->blast->next = b;
	else
		q->bfirst = b;
	q->dlen += blocklen(b);
	while (b->next)
		b = b->next;
	q->blast = b;
}

static size_t read_from_block(struct block *b, uint8_t *to, size_t amt)
{
	size_t copy_amt, retval = 0;
	struct extra_bdata *ebd;

	copy_amt = MIN(BHLEN(b), amt);
	memcpy(to, b->rp, copy_amt);
	/* advance the rp, since this block not be completely consumed and future
	 * reads need to know where to pick up from */
	b->rp += copy_amt;
	to += copy_amt;
	amt -= copy_amt;
	retval += copy_amt;
	for (int i = 0; (i < b->nr_extra_bufs) && amt; i++) {
		ebd = &b->extra_data[i];
		/* skip empty entires.  if we track this in the struct block, we can
		 * just start the for loop early */
		if (!ebd->base || !ebd->len)
			continue;
		copy_amt = MIN(ebd->len, amt);
		memcpy(to, (void*)(ebd->base + ebd->off), copy_amt);
		/* we're actually consuming the entries, just like how we advance rp up
		 * above, and might only consume part of one. */
		ebd->len -= copy_amt;
		ebd->off += copy_amt;
		b->extra_len -= copy_amt;
		if (!ebd->len) {
			/* we don't actually have to decref here.  it's also done in
			 * freeb().  this is the earliest we can free. */
			kfree((void*)ebd->base);
			ebd->base = ebd->off = 0;
		}
		to += copy_amt;
		amt -= copy_amt;
		retval += copy_amt;
	}
	return retval;
}

/*
 *  copy the contents of a string of blocks into
 *  memory.  emptied blocks are freed.  return
 *  pointer to first unconsumed block.
 */
struct block *bl2mem(uint8_t * p, struct block *b, int n)
{
	int i;
	struct block *next;

	/* could be slicker here, since read_from_block is smart */
	for (; b != NULL; b = next) {
		i = BLEN(b);
		if (i > n) {
			/* partial block, consume some */
			read_from_block(b, p, n);
			return b;
		}
		/* full block, consume all and move on */
		i = read_from_block(b, p, i);
		n -= i;
		p += i;
		next = b->next;
		freeb(b);
	}
	return NULL;
}

/* Extract the contents of all blocks and copy to va, up to len.  Returns the
 * actual amount copied. */
static size_t read_all_blocks(struct block *b, void *va, size_t len)
{
	size_t sofar = 0;
	struct block *next;

	do {
		/* We should be draining every block completely. */
		assert(BLEN(b) <= len - sofar);
		assert(va);
		assert(va + sofar);
		assert(b->rp);
		sofar += read_from_block(b, va + sofar, len - sofar);
		next = b->next;
		freeb(b);
		b = next;
	} while (b);
	return sofar;
}

/*
 *  copy the contents of memory into a string of blocks.
 *  return NULL on error.
 */
struct block *mem2bl(uint8_t * p, int len)
{
	ERRSTACK(1);
	int n;
	struct block *b, *first, **l;

	first = NULL;
	l = &first;
	if (waserror()) {
		freeblist(first);
		nexterror();
	}
	do {
		n = len;
		if (n > Maxatomic)
			n = Maxatomic;

		*l = b = block_alloc(n, MEM_WAIT);
		/* TODO consider extra_data */
		memmove(b->wp, p, n);
		b->wp += n;
		p += n;
		len -= n;
		l = &b->next;
	} while (len > 0);
	poperror();

	return first;
}

/*
 *  put a block back to the front of the queue
 *  called with q ilocked
 */
void qputback(struct queue *q, struct block *b)
{
	b->next = q->bfirst;
	if (q->bfirst == NULL)
		q->blast = b;
	q->bfirst = b;
	q->dlen += BLEN(b);
}

/*
 *  get next block from a queue (up to a limit)
 *
 */
struct block *qbread(struct queue *q, size_t len)
{
	return __qbread(q, len, QIO_JUST_ONE_BLOCK | QIO_CAN_ERR_SLEEP, MEM_WAIT);
}

struct block *qbread_nonblock(struct queue *q, size_t len)
{
	return __qbread(q, len, QIO_JUST_ONE_BLOCK | QIO_CAN_ERR_SLEEP |
	                QIO_NON_BLOCK, MEM_WAIT);
}

/* read up to len from a queue into vp. */
size_t qread(struct queue *q, void *va, size_t len)
{
	struct block *blist = __qbread(q, len, QIO_CAN_ERR_SLEEP, MEM_WAIT);

	if (!blist)
		return 0;
	return read_all_blocks(blist, va, len);
}

size_t qread_nonblock(struct queue *q, void *va, size_t len)
{
	struct block *blist = __qbread(q, len, QIO_CAN_ERR_SLEEP | QIO_NON_BLOCK,
	                               MEM_WAIT);

	if (!blist)
		return 0;
	return read_all_blocks(blist, va, len);
}

static int qnotfull(void *a)
{
	struct queue *q = a;

	return qwritable(q) || (q->state & Qclosed);
}

/* Helper: enqueues a list of blocks to a queue.  Returns the total length. */
static size_t enqueue_blist(struct queue *q, struct block *b)
{
	size_t dlen;

	if (q->bfirst)
		q->blast->next = b;
	else
		q->bfirst = b;
	dlen = BLEN(b);
	while (b->next) {
		b = b->next;
		dlen += BLEN(b);
	}
	q->blast = b;
	q->dlen += dlen;
	return dlen;
}

/* Adds block (which can be a list of blocks) to the queue, subject to
 * qio_flags.  Returns the length written on success or -1 on non-throwable
 * error.  Adjust qio_flags to control the value-added features!. */
static ssize_t __qbwrite(struct queue *q, struct block *b, int qio_flags)
{
	ssize_t ret;
	bool was_unreadable;

	if (q->bypass) {
		ret = blocklen(b);
		(*q->bypass) (q->arg, b);
		return ret;
	}
	spin_lock_irqsave(&q->lock);
	was_unreadable = q->dlen == 0;
	if (q->state & Qclosed) {
		spin_unlock_irqsave(&q->lock);
		freeblist(b);
		if (!(qio_flags & QIO_CAN_ERR_SLEEP))
			return -1;
		if (q->err[0])
			error(EPIPE, q->err);
		else
			error(EPIPE, "connection closed");
	}
	if ((qio_flags & QIO_LIMIT) && (q->dlen >= q->limit)) {
		/* drop overflow takes priority over regular non-blocking */
		if ((qio_flags & QIO_DROP_OVERFLOW) || (q->state & Qdropoverflow)) {
			spin_unlock_irqsave(&q->lock);
			freeb(b);
			return -1;
		}
		/* People shouldn't set NON_BLOCK without CAN_ERR, but we can be nice
		 * and catch it. */
		if ((qio_flags & QIO_CAN_ERR_SLEEP) && (qio_flags & QIO_NON_BLOCK)) {
			spin_unlock_irqsave(&q->lock);
			freeb(b);
			error(EAGAIN, "queue full");
		}
	}
	ret = enqueue_blist(q, b);
	QDEBUG checkb(b, "__qbwrite");
	spin_unlock_irqsave(&q->lock);
	/* TODO: not sure if the usage of a kick is mutually exclusive with a
	 * wakeup, meaning that actual users either want a kick or have qreaders. */
	if (q->kick && (was_unreadable || (q->state & Qkick)))
		q->kick(q->arg);
	if (was_unreadable) {
		/* Unlike the read side, there's no double-check to make sure the queue
		 * transitioned across an edge.  We know we added something, so that's
		 * enough.  We wake if the queue was empty.  Both sides are the same, in
		 * that the condition for which we do the rendez_wakeup() is the same as
		 * the condition done for the rendez_sleep(). */
		rendez_wakeup(&q->rr);
		qwake_cb(q, FDTAP_FILT_READABLE);
	}
	/*
	 *  flow control, wait for queue to get below the limit
	 *  before allowing the process to continue and queue
	 *  more.  We do this here so that postnote can only
	 *  interrupt us after the data has been queued.  This
	 *  means that things like 9p flushes and ssl messages
	 *  will not be disrupted by software interrupts.
	 *
	 *  Note - this is moderately dangerous since a process
	 *  that keeps getting interrupted and rewriting will
	 *  queue infinite crud.
	 */
	if ((qio_flags & QIO_CAN_ERR_SLEEP) &&
	    !(q->state & Qdropoverflow) && !(qio_flags & QIO_NON_BLOCK)) {
		/* This is a racy peek at the q status.  If we accidentally block, our
		 * rendez will return.  The rendez's peak (qnotfull) is also racy w.r.t.
		 * the q's spinlock (that lock protects writes, but not reads).
		 *
		 * Here's the deal: when holding the rendez lock, if we see the sleep
		 * condition, the consumer will wake us.  The condition will only ever
		 * be changed by the next qbread() (consumer, changes q->dlen).  That
		 * code will do a rendez wake, which will spin on the rendez lock,
		 * meaning it won't procede until we either see the new state (and
		 * return) or put ourselves on the rendez, and wake up.
		 *
		 * The pattern is one side writes mem, then signals.  Our side checks
		 * the signal, then reads the mem.  The goal is to not miss seeing the
		 * signal AND missing the memory write.  In this specific case, the
		 * signal is actually synchronous (the rendez lock) and not basic shared
		 * memory.
		 *
		 * Oh, and we spin in case we woke early and someone else filled the
		 * queue, mesa-style. */
		while (!qnotfull(q))
			rendez_sleep(&q->wr, qnotfull, q);
	}
	return ret;
}

/*
 *  add a block to a queue obeying flow control
 */
ssize_t qbwrite(struct queue *q, struct block *b)
{
	return __qbwrite(q, b, QIO_CAN_ERR_SLEEP | QIO_LIMIT);
}

ssize_t qbwrite_nonblock(struct queue *q, struct block *b)
{
	return __qbwrite(q, b, QIO_CAN_ERR_SLEEP | QIO_LIMIT | QIO_NON_BLOCK);
}

ssize_t qibwrite(struct queue *q, struct block *b)
{
	return __qbwrite(q, b, 0);
}

/* Helper, allocs a block and copies [from, from + len) into it.  Returns the
 * block on success, 0 on failure. */
static struct block *build_block(void *from, size_t len, int mem_flags)
{
	struct block *b;
	void *ext_buf;

	/* If len is small, we don't need to bother with the extra_data.  But until
	 * the whole stack can handle extd blocks, we'll use them unconditionally.
	 * */
#ifdef CONFIG_BLOCK_EXTRAS
	/* allocb builds in 128 bytes of header space to all blocks, but this is
	 * only available via padblock (to the left).  we also need some space
	 * for pullupblock for some basic headers (like icmp) that get written
	 * in directly */
	b = block_alloc(64, mem_flags);
	if (!b)
		return 0;
	ext_buf = kmalloc(len, mem_flags);
	if (!ext_buf) {
		kfree(b);
		return 0;
	}
	memcpy(ext_buf, from, len);
	if (block_add_extd(b, 1, mem_flags)) {
		kfree(ext_buf);
		kfree(b);
		return 0;
	}
	b->extra_data[0].base = (uintptr_t)ext_buf;
	b->extra_data[0].off = 0;
	b->extra_data[0].len = len;
	b->extra_len += len;
#else
	b = block_alloc(len, mem_flags);
	if (!b)
		return 0;
	memmove(b->wp, from, len);
	b->wp += len;
#endif
	return b;
}

static ssize_t __qwrite(struct queue *q, void *vp, size_t len, int mem_flags,
                        int qio_flags)
{
	size_t n, sofar;
	struct block *b;
	uint8_t *p = vp;
	void *ext_buf;

	sofar = 0;
	do {
		n = len - sofar;
		/* This is 64K, the max amount per single block.  Still a good value? */
		if (n > Maxatomic)
			n = Maxatomic;
		b = build_block(p + sofar, n, mem_flags);
		if (!b)
			break;
		if (__qbwrite(q, b, qio_flags) < 0)
			break;
		sofar += n;
	} while ((sofar < len) && (q->state & Qmsg) == 0);
	return sofar;
}

ssize_t qwrite(struct queue *q, void *vp, int len)
{
	return __qwrite(q, vp, len, MEM_WAIT, QIO_CAN_ERR_SLEEP | QIO_LIMIT);
}

ssize_t qwrite_nonblock(struct queue *q, void *vp, int len)
{
	return __qwrite(q, vp, len, MEM_WAIT, QIO_CAN_ERR_SLEEP | QIO_LIMIT |
	                                      QIO_NON_BLOCK);
}

ssize_t qiwrite(struct queue *q, void *vp, int len)
{
	return __qwrite(q, vp, len, MEM_ATOMIC, 0);
}

/*
 *  be extremely careful when calling this,
 *  as there is no reference accounting
 */
void qfree(struct queue *q)
{
	qclose(q);
	kfree(q);
}

/*
 *  Mark a queue as closed.  No further IO is permitted.
 *  All blocks are released.
 */
void qclose(struct queue *q)
{
	struct block *bfirst;

	if (q == NULL)
		return;

	/* mark it */
	spin_lock_irqsave(&q->lock);
	q->state |= Qclosed;
	q->state &= ~Qdropoverflow;
	q->err[0] = 0;
	bfirst = q->bfirst;
	q->bfirst = 0;
	q->dlen = 0;
	spin_unlock_irqsave(&q->lock);

	/* free queued blocks */
	freeblist(bfirst);

	/* wake up readers/writers */
	rendez_wakeup(&q->rr);
	rendez_wakeup(&q->wr);
	qwake_cb(q, FDTAP_FILT_HANGUP);
}

/* Mark a queue as closed.  Wakeup any readers.  Don't remove queued blocks.
 *
 * msg will be the errstr received by any waiters (qread, qbread, etc).  If
 * there is no message, which is what also happens during a natural qclose(),
 * those waiters will simply return 0.  qwriters will always error() on a
 * closed/hungup queue. */
void qhangup(struct queue *q, char *msg)
{
	/* mark it */
	spin_lock_irqsave(&q->lock);
	q->state |= Qclosed;
	if (msg == 0 || *msg == 0)
		q->err[0] = 0;
	else
		strlcpy(q->err, msg, ERRMAX);
	spin_unlock_irqsave(&q->lock);

	/* wake up readers/writers */
	rendez_wakeup(&q->rr);
	rendez_wakeup(&q->wr);
	qwake_cb(q, FDTAP_FILT_HANGUP);
}

/*
 *  return non-zero if the q is hungup
 */
int qisclosed(struct queue *q)
{
	return q->state & Qclosed;
}

/*
 *  mark a queue as no longer hung up.  resets the wake_cb.
 */
void qreopen(struct queue *q)
{
	spin_lock_irqsave(&q->lock);
	q->state &= ~Qclosed;
	q->eof = 0;
	q->limit = q->inilim;
	q->wake_cb = 0;
	q->wake_data = 0;
	spin_unlock_irqsave(&q->lock);
}

/*
 *  return bytes queued
 */
int qlen(struct queue *q)
{
	return q->dlen;
}

/*
 * return space remaining before flow control
 *
 *  This used to be
 *  q->len < q->limit/2
 *  but it slows down tcp too much for certain write sizes.
 *  I really don't understand it completely.  It may be
 *  due to the queue draining so fast that the transmission
 *  stalls waiting for the app to produce more data.  - presotto
 *
 *  q->len was the amount of bytes, which is no longer used.  we now use
 *  q->dlen, the amount of usable data.  a.k.a. qlen()...  - brho
 */
int qwindow(struct queue *q)
{
	int l;

	l = q->limit - q->dlen;
	if (l < 0)
		l = 0;
	return l;
}

/*
 *  return true if we can read without blocking
 */
int qcanread(struct queue *q)
{
	return q->bfirst != 0;
}

/*
 *  change queue limit
 */
void qsetlimit(struct queue *q, int limit)
{
	q->limit = limit;
}

/*
 *  set whether writes drop overflowing blocks, or if we sleep
 */
void qdropoverflow(struct queue *q, bool onoff)
{
	spin_lock_irqsave(&q->lock);
	if (onoff)
		q->state |= Qdropoverflow;
	else
		q->state &= ~Qdropoverflow;
	spin_unlock_irqsave(&q->lock);
}

/* Be careful: this can affect concurrent reads/writes and code that might have
 * built-in expectations of the q's type. */
void q_toggle_qmsg(struct queue *q, bool onoff)
{
	spin_lock_irqsave(&q->lock);
	if (onoff)
		q->state |= Qmsg;
	else
		q->state &= ~Qmsg;
	spin_unlock_irqsave(&q->lock);
}

/* Be careful: this can affect concurrent reads/writes and code that might have
 * built-in expectations of the q's type. */
void q_toggle_qcoalesce(struct queue *q, bool onoff)
{
	spin_lock_irqsave(&q->lock);
	if (onoff)
		q->state |= Qcoalesce;
	else
		q->state &= ~Qcoalesce;
	spin_unlock_irqsave(&q->lock);
}

/*
 *  flush the output queue
 */
void qflush(struct queue *q)
{
	struct block *bfirst;

	/* mark it */
	spin_lock_irqsave(&q->lock);
	bfirst = q->bfirst;
	q->bfirst = 0;
	q->dlen = 0;
	spin_unlock_irqsave(&q->lock);

	/* free queued blocks */
	freeblist(bfirst);

	/* wake up writers */
	rendez_wakeup(&q->wr);
	qwake_cb(q, FDTAP_FILT_WRITABLE);
}

int qfull(struct queue *q)
{
	return q->dlen >= q->limit;
}

int qstate(struct queue *q)
{
	return q->state;
}

void qdump(struct queue *q)
{
	if (q)
		printk("q=%p bfirst=%p blast=%p dlen=%d limit=%d state=#%x\n",
			   q, q->bfirst, q->blast, q->dlen, q->limit, q->state);
}

/* On certain wakeup events, qio will call func(q, data, filter), where filter
 * marks the type of wakeup event (flags from FDTAP).
 *
 * There's no sync protection.  If you change the CB while the qio is running,
 * you might get a CB with the data or func from a previous set_wake_cb.  You
 * should set this once per queue and forget it.
 *
 * You can remove the CB by passing in 0 for the func.  Alternatively, you can
 * just make sure that the func(data) pair are valid until the queue is freed or
 * reopened. */
void qio_set_wake_cb(struct queue *q, qio_wake_cb_t func, void *data)
{
	q->wake_data = data;
	wmb();	/* if we see func, we'll also see the data for it */
	q->wake_cb = func;
}

/* Helper for detecting whether we'll block on a read at this instant. */
bool qreadable(struct queue *q)
{
	return qlen(q) > 0;
}

/* Helper for detecting whether we'll block on a write at this instant. */
bool qwritable(struct queue *q)
{
	return !q->limit || qwindow(q) > 0;
}
