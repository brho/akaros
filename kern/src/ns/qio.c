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

	int len;					/* bytes allocated to queue */
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
};

unsigned int qiomaxatomic = Maxatomic;

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
 *  free a list of blocks
 */
void freeblist(struct block *b)
{
	struct block *next;

	for (; b != 0; b = next) {
		next = b->next;
		b->next = 0;
		freeb(b);
	}
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

/* Returns a block with the remaining contents of b all in the main body of the
 * returned block.  Replace old references to b with the returned value (which
 * may still be 'b', if no change was needed. */
struct block *linearizeblock(struct block *b)
{
	struct block *newb;
	size_t len;
	struct extra_bdata *ebd;

	if (!b->extra_len)
		return b;

	newb = block_alloc(BLEN(b), MEM_WAIT);
	len = BHLEN(b);
	memcpy(newb->wp, b->rp, len);
	newb->wp += len;
	len = b->extra_len;
	for (int i = 0; (i < b->nr_extra_bufs) && len; i++) {
		ebd = &b->extra_data[i];
		if (!ebd->base || !ebd->len)
			continue;
		memcpy(newb->wp, (void*)(ebd->base + ebd->off), ebd->len);
		newb->wp += ebd->len;
		len -= ebd->len;
	}
	/* TODO: any other flags that need copied over? */
	if (b->flag & BCKSUM_FLAGS) {
		newb->flag |= (b->flag & BCKSUM_FLAGS);
		newb->checksum_start = b->checksum_start;
		newb->checksum_offset = b->checksum_offset;
		newb->mss = b->mss;
	}
	freeb(b);
	return newb;
}

/*
 *  make sure the first block has at least n bytes in its main body
 */
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

	 /* a start at explicit main-body / header management */
	if (bp->extra_len) {
		if (n > bp->lim - bp->rp) {
			/* would need to realloc a new block and copy everything over. */
			panic("can't pullup %d bytes, no place to put it: bp->lim %p, bp->rp %p, bp->lim-bp->rp %d\n",
					n, bp->lim, bp->rp, bp->lim-bp->rp);
		}
		len = n - BHLEN(bp);
		if (len > bp->extra_len)
			panic("pullup more than extra (%d, %d, %d)\n",
			      n, BHLEN(bp), bp->extra_len);
		checkb(bp, "before pullup");
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
		checkb(bp, "after pullup");
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

/*
 *  copy 'count' bytes into a new block
 */
struct block *copyblock(struct block *bp, int count)
{
	int l;
	struct block *nbp;

	QDEBUG checkb(bp, "copyblock 0");
	nbp = block_alloc(count, MEM_WAIT);
	if (bp->flag & BCKSUM_FLAGS) {
		nbp->flag |= (bp->flag & BCKSUM_FLAGS);
		nbp->checksum_start = bp->checksum_start;
		nbp->checksum_offset = bp->checksum_offset;
		nbp->mss = bp->mss;
	}
	PANIC_EXTRA(bp);
	for (; count > 0 && bp != 0; bp = bp->next) {
		l = BLEN(bp);
		if (l > count)
			l = count;
		memmove(nbp->wp, bp->rp, l);
		nbp->wp += l;
		count -= l;
	}
	if (count > 0) {
		memset(nbp->wp, 0, count);
		nbp->wp += count;
	}
	copyblockcnt++;
	QDEBUG checkb(nbp, "copyblock 1");

	return nbp;
}

/* Adjust block @bp so that its size is exactly @len.
 * If the size is increased, fill in the new contents with zeros.
 * If the size is decreased, discard some of the old contents at the tail. */
struct block *adjustblock(struct block *bp, int len)
{
	struct extra_bdata *ebd;
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
		block_append_extra(bp, len - BLEN(bp), MEM_WAIT);
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


/*
 *  get next block from a queue, return null if nothing there
 */
struct block *qget(struct queue *q)
{
	int dowakeup;
	struct block *b;

	/* sync with qwrite */
	spin_lock_irqsave(&q->lock);

	b = q->bfirst;
	if (b == NULL) {
		q->state |= Qstarve;
		spin_unlock_irqsave(&q->lock);
		return NULL;
	}
	q->bfirst = b->next;
	b->next = 0;
	q->len -= BALLOC(b);
	q->dlen -= BLEN(b);
	QDEBUG checkb(b, "qget");

	/* if writer flow controlled, restart */
	if ((q->state & Qflow) && q->len < q->limit / 2) {
		q->state &= ~Qflow;
		dowakeup = 1;
	} else
		dowakeup = 0;

	spin_unlock_irqsave(&q->lock);

	if (dowakeup)
		rendez_wakeup(&q->wr);
	qwake_cb(q, FDTAP_FILT_WRITABLE);

	return b;
}

/*
 *  throw away the next 'len' bytes in the queue
 * returning the number actually discarded
 */
int qdiscard(struct queue *q, int len)
{
	struct block *b;
	int dowakeup, n, sofar, body_amt, extra_amt;
	struct extra_bdata *ebd;

	spin_lock_irqsave(&q->lock);
	for (sofar = 0; sofar < len; sofar += n) {
		b = q->bfirst;
		if (b == NULL)
			break;
		QDEBUG checkb(b, "qdiscard");
		n = BLEN(b);
		if (n <= len - sofar) {
			q->bfirst = b->next;
			b->next = 0;
			q->len -= BALLOC(b);
			q->dlen -= BLEN(b);
			freeb(b);
		} else {
			n = len - sofar;
			q->dlen -= n;
			/* partial block removal */
			body_amt = MIN(BHLEN(b), n);
			b->rp += body_amt;
			extra_amt = n - body_amt;
			/* reduce q->len by the amount we remove from the extras.  The
			 * header will always be accounted for above, during block removal.
			 * */
			q->len -= extra_amt;
			for (int i = 0; (i < b->nr_extra_bufs) && extra_amt; i++) {
				ebd = &b->extra_data[i];
				if (!ebd->base || !ebd->len)
					continue;
				if (extra_amt >= ebd->len) {
					/* remove the entire entry, note the kfree release */
					b->extra_len -= ebd->len;
					extra_amt -= ebd->len;
					kfree((void*)ebd->base);
					ebd->base = ebd->off = ebd->len = 0;
					continue;
				}
				ebd->off += extra_amt;
				ebd->len -= extra_amt;
				b->extra_len -= extra_amt;
				extra_amt = 0;
			}
		}
	}

	/*
	 *  if writer flow controlled, restart
	 *
	 *  This used to be
	 *  q->len < q->limit/2
	 *  but it slows down tcp too much for certain write sizes.
	 *  I really don't understand it completely.  It may be
	 *  due to the queue draining so fast that the transmission
	 *  stalls waiting for the app to produce more data.  - presotto
	 */
	if ((q->state & Qflow) && q->len < q->limit) {
		q->state &= ~Qflow;
		dowakeup = 1;
	} else
		dowakeup = 0;

	spin_unlock_irqsave(&q->lock);

	if (dowakeup)
		rendez_wakeup(&q->wr);
	qwake_cb(q, FDTAP_FILT_WRITABLE);

	return sofar;
}

/*
 *  Interrupt level copy out of a queue, return # bytes copied.
 */
int qconsume(struct queue *q, void *vp, int len)
{
	struct block *b;
	int n, dowakeup;
	uint8_t *p = vp;
	struct block *tofree = NULL;

	/* sync with qwrite */
	spin_lock_irqsave(&q->lock);

	for (;;) {
		b = q->bfirst;
		if (b == 0) {
			q->state |= Qstarve;
			spin_unlock_irqsave(&q->lock);
			return -1;
		}
		QDEBUG checkb(b, "qconsume 1");

		n = BLEN(b);
		if (n > 0)
			break;
		q->bfirst = b->next;
		q->len -= BALLOC(b);

		/* remember to free this */
		b->next = tofree;
		tofree = b;
	};

	PANIC_EXTRA(b);
	if (n < len)
		len = n;
	memmove(p, b->rp, len);
	consumecnt += n;
	b->rp += len;
	q->dlen -= len;

	/* discard the block if we're done with it */
	if ((q->state & Qmsg) || len == n) {
		q->bfirst = b->next;
		b->next = 0;
		q->len -= BALLOC(b);
		q->dlen -= BLEN(b);

		/* remember to free this */
		b->next = tofree;
		tofree = b;
	}

	/* if writer flow controlled, restart */
	if ((q->state & Qflow) && q->len < q->limit / 2) {
		q->state &= ~Qflow;
		dowakeup = 1;
	} else
		dowakeup = 0;

	spin_unlock_irqsave(&q->lock);

	if (dowakeup)
		rendez_wakeup(&q->wr);
	qwake_cb(q, FDTAP_FILT_WRITABLE);

	if (tofree != NULL)
		freeblist(tofree);

	return len;
}

int qpass(struct queue *q, struct block *b)
{
	int dlen, len, dowakeup;
	bool was_empty;

	/* sync with qread */
	dowakeup = 0;
	spin_lock_irqsave(&q->lock);
	was_empty = q->len == 0;
	if (q->len >= q->limit) {
		freeblist(b);
		spin_unlock_irqsave(&q->lock);
		return -1;
	}
	if (q->state & Qclosed) {
		len = blocklen(b);
		freeblist(b);
		spin_unlock_irqsave(&q->lock);
		return len;
	}

	/* add buffer to queue */
	if (q->bfirst)
		q->blast->next = b;
	else
		q->bfirst = b;
	len = BALLOC(b);
	dlen = BLEN(b);
	QDEBUG checkb(b, "qpass");
	while (b->next) {
		b = b->next;
		QDEBUG checkb(b, "qpass");
		len += BALLOC(b);
		dlen += BLEN(b);
	}
	q->blast = b;
	q->len += len;
	q->dlen += dlen;

	if (q->len >= q->limit / 2)
		q->state |= Qflow;

	if (q->state & Qstarve) {
		q->state &= ~Qstarve;
		dowakeup = 1;
	}
	spin_unlock_irqsave(&q->lock);

	if (dowakeup)
		rendez_wakeup(&q->rr);
	if (was_empty)
		qwake_cb(q, FDTAP_FILT_READABLE);

	return len;
}

int qpassnolim(struct queue *q, struct block *b)
{
	int dlen, len, dowakeup;
	bool was_empty;

	/* sync with qread */
	dowakeup = 0;
	spin_lock_irqsave(&q->lock);
	was_empty = q->len == 0;

	if (q->state & Qclosed) {
		freeblist(b);
		spin_unlock_irqsave(&q->lock);
		return BALLOC(b);
	}

	/* add buffer to queue */
	if (q->bfirst)
		q->blast->next = b;
	else
		q->bfirst = b;
	len = BALLOC(b);
	dlen = BLEN(b);
	QDEBUG checkb(b, "qpass");
	while (b->next) {
		b = b->next;
		QDEBUG checkb(b, "qpass");
		len += BALLOC(b);
		dlen += BLEN(b);
	}
	q->blast = b;
	q->len += len;
	q->dlen += dlen;

	if (q->len >= q->limit / 2)
		q->state |= Qflow;

	if (q->state & Qstarve) {
		q->state &= ~Qstarve;
		dowakeup = 1;
	}
	spin_unlock_irqsave(&q->lock);

	if (dowakeup)
		rendez_wakeup(&q->rr);
	if (was_empty)
		qwake_cb(q, FDTAP_FILT_READABLE);

	return len;
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

int qproduce(struct queue *q, void *vp, int len)
{
	struct block *b;
	int dowakeup;
	uint8_t *p = vp;
	bool was_empty;

	/* sync with qread */
	dowakeup = 0;
	spin_lock_irqsave(&q->lock);
	was_empty = q->len == 0;

	/* no waiting receivers, room in buffer? */
	if (q->len >= q->limit) {
		q->state |= Qflow;
		spin_unlock_irqsave(&q->lock);
		return -1;
	}

	/* save in buffer */
	/* use Qcoalesce here to save storage */
	// TODO: Consider removing the Qcoalesce flag and force a coalescing
	// strategy by default.
	b = q->blast;
	if ((q->state & Qcoalesce) == 0 || q->bfirst == NULL
		|| b->lim - b->wp < len) {
		/* need a new block */
		b = block_alloc(len, MEM_ATOMIC);
		if (b == 0) {
			spin_unlock_irqsave(&q->lock);
			return 0;
		}
		if (q->bfirst)
			q->blast->next = b;
		else
			q->bfirst = b;
		q->blast = b;
		/* b->next = 0; done by iallocb() */
		q->len += BALLOC(b);
	}
	PANIC_EXTRA(b);
	memmove(b->wp, p, len);
	producecnt += len;
	b->wp += len;
	q->dlen += len;
	QDEBUG checkb(b, "qproduce");

	if (q->state & Qstarve) {
		q->state &= ~Qstarve;
		dowakeup = 1;
	}

	if (q->len >= q->limit)
		q->state |= Qflow;
	spin_unlock_irqsave(&q->lock);

	if (dowakeup)
		rendez_wakeup(&q->rr);
	if (was_empty)
		qwake_cb(q, FDTAP_FILT_READABLE);

	return len;
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

/* given a string of blocks, fills the new block's extra_data  with the contents
 * of the blist [offset, len + offset)
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
 * new blocks ext_data. */
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
	q->state |= Qstarve;
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
static bool qwait_and_ilock(struct queue *q)
{
	while (1) {
		spin_lock_irqsave(&q->lock);
		if (q->bfirst != NULL)
			return TRUE;
		if (q->state & Qclosed) {
			if (++q->eof > 3) {
				spin_unlock_irqsave(&q->lock);
				error(EFAIL, "multiple reads on a closed queue");
			}
			if (q->err[0]) {
				spin_unlock_irqsave(&q->lock);
				error(EFAIL, q->err);
			}
			return FALSE;
		}
		/* We set Qstarve regardless of whether we are non-blocking or not.
		 * Qstarve tracks the edge detection of the queue being empty. */
		q->state |= Qstarve;
		if (q->state & Qnonblock) {
			spin_unlock_irqsave(&q->lock);
			error(EAGAIN, "queue empty");
		}
		spin_unlock_irqsave(&q->lock);
		/* may throw an error() */
		rendez_sleep(&q->rr, notempty, q);
	}
}

/*
 * add a block list to a queue
 */
void qaddlist(struct queue *q, struct block *b)
{
	/* TODO: q lock? */
	/* queue the block */
	if (q->bfirst)
		q->blast->next = b;
	else
		q->bfirst = b;
	q->len += blockalloclen(b);
	q->dlen += blocklen(b);
	while (b->next)
		b = b->next;
	q->blast = b;
}

/*
 *  called with q ilocked
 */
struct block *qremove(struct queue *q)
{
	struct block *b;

	b = q->bfirst;
	if (b == NULL)
		return NULL;
	q->bfirst = b->next;
	b->next = NULL;
	q->dlen -= BLEN(b);
	q->len -= BALLOC(b);
	QDEBUG checkb(b, "qremove");
	return b;
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
	q->len += BALLOC(b);
	q->dlen += BLEN(b);
}

/*
 *  flow control, get producer going again
 *  called with q ilocked
 */
static void qwakeup_iunlock(struct queue *q)
{
	int dowakeup = 0;

	/* if writer flow controlled, restart */
	if ((q->state & Qflow) && q->len < q->limit / 2) {
		q->state &= ~Qflow;
		dowakeup = 1;
	}

	spin_unlock_irqsave(&q->lock);

	/* wakeup flow controlled writers */
	if (dowakeup) {
		if (q->kick)
			q->kick(q->arg);
		rendez_wakeup(&q->wr);
	}
	qwake_cb(q, FDTAP_FILT_WRITABLE);
}

/*
 *  get next block from a queue (up to a limit)
 */
struct block *qbread(struct queue *q, int len)
{
	struct block *b, *nb;
	int n;

	if (!qwait_and_ilock(q)) {
		/* queue closed */
		spin_unlock_irqsave(&q->lock);
		return NULL;
	}

	/* if we get here, there's at least one block in the queue */
	b = qremove(q);
	n = BLEN(b);

	/* split block if it's too big and this is not a message queue */
	nb = b;
	if (n > len) {
		PANIC_EXTRA(b);
		if ((q->state & Qmsg) == 0) {
			n -= len;
			b = block_alloc(n, MEM_WAIT);
			memmove(b->wp, nb->rp + len, n);
			b->wp += n;
			qputback(q, b);
		}
		nb->wp = nb->rp + len;
	}

	/* restart producer */
	qwakeup_iunlock(q);

	return nb;
}

/*
 *  read a queue.  if no data is queued, post a struct block
 *  and wait on its Rendez.
 */
long qread(struct queue *q, void *vp, int len)
{
	struct block *b, *first, **l;
	int m, n;

again:
	if (!qwait_and_ilock(q)) {
		/* queue closed */
		spin_unlock_irqsave(&q->lock);
		return 0;
	}

	/* if we get here, there's at least one block in the queue */
	// TODO: Consider removing the Qcoalesce flag and force a coalescing
	// strategy by default.
	if (q->state & Qcoalesce) {
		/* when coalescing, 0 length blocks just go away */
		b = q->bfirst;
		if (BLEN(b) <= 0) {
			freeb(qremove(q));
			spin_unlock_irqsave(&q->lock);
			goto again;
		}

		/*  grab the first block plus as many
		 *  following blocks as will completely
		 *  fit in the read.
		 */
		n = 0;
		l = &first;
		m = BLEN(b);
		for (;;) {
			*l = qremove(q);
			l = &b->next;
			n += m;

			b = q->bfirst;
			if (b == NULL)
				break;
			m = BLEN(b);
			if (n + m > len)
				break;
		}
	} else {
		first = qremove(q);
		n = BLEN(first);
	}
	b = bl2mem(vp, first, len);
	/* take care of any left over partial block */
	if (b != NULL) {
		n -= BLEN(b);
		if (q->state & Qmsg)
			freeb(b);
		else
			qputback(q, b);
	}

	/* restart producer */
	qwakeup_iunlock(q);

	return n;
}

static int qnotfull(void *a)
{
	struct queue *q = a;

	return q->len < q->limit || (q->state & Qclosed);
}

uint32_t dropcnt;

/*
 *  add a block to a queue obeying flow control
 */
long qbwrite(struct queue *q, struct block *b)
{
	int n, dowakeup;
	bool was_empty;

	n = BLEN(b);

	if (q->bypass) {
		(*q->bypass) (q->arg, b);
		return n;
	}

	dowakeup = 0;

	spin_lock_irqsave(&q->lock);
	was_empty = q->len == 0;

	/* give up if the queue is closed */
	if (q->state & Qclosed) {
		spin_unlock_irqsave(&q->lock);
		freeb(b);
		if (q->err[0])
			error(EFAIL, q->err);
		else
			error(EFAIL, "connection closed");
	}

	/* if nonblocking, don't queue over the limit */
	if (q->len >= q->limit) {
		/* drop overflow takes priority over regular non-blocking */
		if (q->state & Qdropoverflow) {
			spin_unlock_irqsave(&q->lock);
			freeb(b);
			dropcnt += n;
			return n;
		}
		if (q->state & Qnonblock) {
			spin_unlock_irqsave(&q->lock);
			freeb(b);
			error(EAGAIN, "queue full");
		}
	}

	/* queue the block */
	if (q->bfirst)
		q->blast->next = b;
	else
		q->bfirst = b;
	q->blast = b;
	b->next = 0;
	q->len += BALLOC(b);
	q->dlen += n;
	QDEBUG checkb(b, "qbwrite");
	b = NULL;

	/* make sure other end gets awakened */
	if (q->state & Qstarve) {
		q->state &= ~Qstarve;
		dowakeup = 1;
	}
	spin_unlock_irqsave(&q->lock);

	/*  get output going again */
	if (q->kick && (dowakeup || (q->state & Qkick)))
		q->kick(q->arg);

	/* wakeup anyone consuming at the other end */
	if (dowakeup)
		rendez_wakeup(&q->rr);
	if (was_empty)
		qwake_cb(q, FDTAP_FILT_READABLE);

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
	for (;;) {
		if ((q->state & (Qdropoverflow | Qnonblock)) || qnotfull(q))
			break;

		spin_lock_irqsave(&q->lock);
		q->state |= Qflow;
		spin_unlock_irqsave(&q->lock);
		rendez_sleep(&q->wr, qnotfull, q);
	}

	return n;
}

long qibwrite(struct queue *q, struct block *b)
{
	int n, dowakeup;
	bool was_empty;

	dowakeup = 0;

	n = BLEN(b);

	spin_lock_irqsave(&q->lock);
	was_empty = q->len == 0;

	QDEBUG checkb(b, "qibwrite");
	if (q->bfirst)
		q->blast->next = b;
	else
		q->bfirst = b;
	q->blast = b;
	q->len += BALLOC(b);
	q->dlen += n;

	if (q->state & Qstarve) {
		q->state &= ~Qstarve;
		dowakeup = 1;
	}

	spin_unlock_irqsave(&q->lock);

	if (dowakeup) {
		if (q->kick)
			q->kick(q->arg);
		rendez_wakeup(&q->rr);
	}
	if (was_empty)
		qwake_cb(q, FDTAP_FILT_READABLE);

	return n;
}

/*
 *  write to a queue.  only Maxatomic bytes at a time is atomic.
 */
int qwrite(struct queue *q, void *vp, int len)
{
	int n, sofar;
	struct block *b;
	uint8_t *p = vp;
	void *ext_buf;

	QDEBUG if (!islo())
		 printd("qwrite hi %p\n", getcallerpc(&q));

	sofar = 0;
	do {
		n = len - sofar;
		/* This is 64K, the max amount per single block.  Still a good value? */
		if (n > Maxatomic)
			n = Maxatomic;

		/* If n is small, we don't need to bother with the extra_data.  But
		 * until the whole stack can handle extd blocks, we'll use them
		 * unconditionally. */
#ifdef CONFIG_BLOCK_EXTRAS
		/* allocb builds in 128 bytes of header space to all blocks, but this is
		 * only available via padblock (to the left).  we also need some space
		 * for pullupblock for some basic headers (like icmp) that get written
		 * in directly */
		b = block_alloc(64, MEM_WAIT);
		ext_buf = kmalloc(n, 0);
		memcpy(ext_buf, p + sofar, n);
		block_add_extd(b, 1, MEM_WAIT); /* returns 0 on success */
		b->extra_data[0].base = (uintptr_t)ext_buf;
		b->extra_data[0].off = 0;
		b->extra_data[0].len = n;
		b->extra_len += n;
#else
		b = block_alloc(n, MEM_WAIT);
		memmove(b->wp, p + sofar, n);
		b->wp += n;
#endif
			
		qbwrite(q, b);

		sofar += n;
	} while (sofar < len && (q->state & Qmsg) == 0);

	return len;
}

/*
 *  used by print() to write to a queue.  Since we may be splhi or not in
 *  a process, don't qlock.
 */
int qiwrite(struct queue *q, void *vp, int len)
{
	int n, sofar;
	struct block *b;
	uint8_t *p = vp;

	sofar = 0;
	do {
		n = len - sofar;
		if (n > Maxatomic)
			n = Maxatomic;

		b = block_alloc(n, MEM_ATOMIC);
		if (b == NULL)
			break;
		/* TODO consider extra_data */
		memmove(b->wp, p + sofar, n);
		/* this adjusts BLEN to be n, or at least it should */
		b->wp += n;
		assert(n == BLEN(b));
		qibwrite(q, b);

		sofar += n;
	} while (sofar < len && (q->state & Qmsg) == 0);

	return sofar;
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
	q->state &= ~(Qflow | Qstarve | Qdropoverflow | Qnonblock);
	q->err[0] = 0;
	bfirst = q->bfirst;
	q->bfirst = 0;
	q->len = 0;
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
	q->state |= Qstarve;
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
 */
int qwindow(struct queue *q)
{
	int l;

	l = q->limit - q->len;
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
	if (onoff)
		q->state |= Qdropoverflow;
	else
		q->state &= ~Qdropoverflow;
}

/* set whether or not the queue is nonblocking, in the EAGAIN sense. */
void qnonblock(struct queue *q, bool onoff)
{
	if (onoff)
		q->state |= Qnonblock;
	else
		q->state &= ~Qnonblock;
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
	q->len = 0;
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
	return q->len >= q->limit;
}

int qstate(struct queue *q)
{
	return q->state;
}

void qdump(struct queue *q)
{
	if (q)
		printk("q=%p bfirst=%p blast=%p len=%d dlen=%d limit=%d state=#%x\n",
			   q, q->bfirst, q->blast, q->len, q->dlen, q->limit, q->state);
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
