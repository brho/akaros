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

static uint32_t padblockcnt;
static uint32_t concatblockcnt;
static uint32_t pullupblockcnt;
static uint32_t copyblockcnt;
static uint32_t consumecnt;
static uint32_t producecnt;
static uint32_t qcopycnt;

static int debugging;

#define QDEBUG	if(0)

enum {
	Maxatomic = 64 * 1024,
};

unsigned int qiomaxatomic = Maxatomic;

void ixsummary(void)
{
	debugging ^= 1;
	iallocsummary();
	printd("pad %lu, concat %lud, pullup %lud, copy %lud\n",
		   padblockcnt, concatblockcnt, pullupblockcnt, copyblockcnt);
	printd("consume %lu, produce %lud, qcopy %lud\n",
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

	QDEBUG checkb(bp, "padblock 1");
	if (size >= 0) {
		if (bp->rp - bp->base >= size) {
			bp->rp -= size;
			return bp;
		}

		if (bp->next)
			panic("padblock %#p", getcallerpc(&bp));
		n = BLEN(bp);
		padblockcnt++;
		nbp = allocb(size + n);
		nbp->rp += size;
		nbp->wp = nbp->rp;
		memmove(nbp->wp, bp->rp, n);
		nbp->wp += n;
		freeb(bp);
		nbp->rp -= size;
	} else {
		size = -size;

		if (bp->next)
			panic("padblock %#p", getcallerpc(&bp));

		if (bp->lim - bp->wp >= size)
			return bp;

		n = BLEN(bp);
		padblockcnt++;
		nbp = allocb(size + n);
		memmove(nbp->wp, bp->rp, n);
		nbp->wp += n;
		freeb(bp);
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

	nb = allocb(blocklen(bp));
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

/*
 *  make sure the first block has at least n bytes
 */
struct block *pullupblock(struct block *bp, int n)
{
	int i;
	struct block *nbp;

	/*
	 *  this should almost always be true, it's
	 *  just to avoid every caller checking.
	 */
	if (BLEN(bp) >= n)
		return bp;

	/*
	 *  if not enough room in the first block,
	 *  add another to the front of the list.
	 */
	if (bp->lim - bp->rp < n) {
		nbp = allocb(n);
		nbp->next = bp;
		bp = nbp;
	}

	/*
	 *  copy bytes from the trailing blocks into the first
	 */
	n -= BLEN(bp);
	for (nbp = bp->next; nbp; nbp = bp->next) {
		i = BLEN(nbp);
		if (i > n) {
			memmove(bp->wp, nbp->rp, n);
			pullupblockcnt++;
			bp->wp += n;
			nbp->rp += n;
			QDEBUG checkb(bp, "pullupblock 1");
			return bp;
		} else {
			/* shouldn't happen but why crash if it does */
			if (i < 0) {
				printd("pullupblock -ve length, from %#p\n", getcallerpc(&bp));
				i = 0;
			}
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
 *  trim to len bytes starting at offset
 */
struct block *trimblock(struct block *bp, int offset, int len)
{
	long l;
	struct block *nb, *startb;

	QDEBUG checkb(bp, "trimblock 1");
	if (blocklen(bp) < offset + len) {
		freeblist(bp);
		return NULL;
	}

	while ((l = BLEN(bp)) < offset) {
		offset -= l;
		nb = bp->next;
		bp->next = NULL;
		freeb(bp);
		bp = nb;
	}

	startb = bp;
	bp->rp += offset;

	while ((l = BLEN(bp)) < len) {
		len -= l;
		bp = bp->next;
	}

	bp->wp -= (BLEN(bp) - len);

	if (bp->next) {
		freeblist(bp->next);
		bp->next = NULL;
	}

	return startb;
}

/*
 *  copy 'count' bytes into a new block
 */
struct block *copyblock(struct block *bp, int count)
{
	int l;
	struct block *nbp;

	QDEBUG checkb(bp, "copyblock 0");
	if (bp->flag & BINTR) {
		nbp = iallocb(count);
		if (nbp == NULL)
			return NULL;
	} else
		nbp = allocb(count);
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

struct block *adjustblock(struct block *bp, int len)
{
	int n;
	struct block *nbp;

	if (len < 0) {
		freeb(bp);
		return NULL;
	}

	if (bp->rp + len > bp->lim) {
		nbp = copyblock(bp, len);
		freeblist(bp);
		QDEBUG checkb(nbp, "adjustblock 1");

		return nbp;
	}

	n = BLEN(bp);
	if (len > n)
		memset(bp->wp, 0, len - n);
	bp->wp = bp->rp + len;
	QDEBUG checkb(bp, "adjustblock 2");

	return bp;
}

/*
 *  throw away up to count bytes from a
 *  list of blocks.  Return count of bytes
 *  thrown away.
 */
int pullblock(struct block **bph, int count)
{
	struct block *bp;
	int n, bytes;

	bytes = 0;
	if (bph == NULL)
		return 0;

	while (*bph != NULL && count != 0) {
		bp = *bph;
		n = BLEN(bp);
		if (count < n)
			n = count;
		bytes += n;
		count -= n;
		bp->rp += n;
		QDEBUG checkb(bp, "pullblock ");
		if (BLEN(bp) == 0) {
			*bph = bp->next;
			bp->next = NULL;
			freeb(bp);
		}
	}
	return bytes;
}

/*
 *  get next block from a queue, return null if nothing there
 * This is an interrupt-level function. 
 */
struct block *qget(struct queue *q)
{
	int dowakeup;
	struct block *b;

	/* sync with qwrite */
	ilock(&q->lock);

	if (q->len <= 0){
		q->state |= Qstarve;
		iunlock(&q->lock);
		return NULL;
	}

	b = qremove(q);
	QDEBUG checkb(b, "qget");

	/* if writer flow controlled, restart */
	/* we're hoping, now, that the piperead wakes them up */
	if ((q->state & Qflow) && q->len < q->limit / 2) {
		q->state &= ~Qflow;
	}
	iunlock(&q->lock);

	return b;
}

/*
 *  throw away the next 'len' bytes in the queue
 */
int qdiscard(struct queue *q, int len)
{
	struct block *b;
	int dowakeup, n, sofar;

	ilock(&q->lock);
	for (sofar = 0; sofar < len; sofar += n) {
		if (q->len <= 0)
			break;

		b = qhead(q);
		QDEBUG checkb(b, "qdiscard");
		n = BLEN(b);
		if (n <= len - sofar) {
			qremove(q);
			b->next = 0;
			q->len -= BALLOC(b);
			q->dlen -= BLEN(b);
			freeb(b);
		} else {
			n = len - sofar;
			b->rp += n;
			q->dlen -= n;
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
	 *
	 *  changed back from q->len < q->limit for reno tcp. - jmk
	 */
	if ((q->state & Qflow) && q->len < q->limit / 2) {
		q->state &= ~Qflow;
	}

	iunlock(&q->lock);

	/* If the length was zero, above, then no read happened. 
	 * so how do we wake up the writer? Well ... the 
	 * act of qremoving at some other place might have done it.
	 */
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
	ilock(&q->lock);

	for (;;) {

		if (qhead(q) == 0) {
			q->state |= Qstarve;
			iunlock(&q->lock);
			return -1;
		}

		b = qhead(q);
		QDEBUG checkb(b, "qconsume 1");
		n = BLEN(b);
		if (n > 0)
			break;
		qremove(q);
		q->len -= BALLOC(b);

		/* remember to free this */
		b->next = tofree;
		tofree = b;
	}
	
	if (n < len)
		len = n;
	memmove(p, b->rp, len);
	consumecnt += n;
	b->rp += len;
	q->dlen -= len;

	/* discard the block if we're done with it */

	if ((q->state & Qmsg) || len == n) {
		qremove(q);
		b->next = 0;
		q->len -= BALLOC(b);
		q->dlen -= BLEN(b);

		/* remember to free this */
		b->next = tofree;
		tofree = b;
	}

	/* if writer flow controlled, restart */
	/* we hope the read will have done this restart. */
	if ((q->state & Qflow) && q->len < q->limit / 2) {
		q->state &= ~Qflow;
	} 

	iunlock(&q->lock);

	if (tofree != NULL)
		freeblist(tofree);

	return len;
}

int qpass(struct queue *q, struct block *b)
{
	int dlen = 0, len = 0, dowakeup;

	/* sync with qread */
	dowakeup = 0;
	ilock(&q->lock);
	if (q->len >= q->limit) {
		freeblist(b);
		iunlock(&q->lock);
		return -1;
	}
	if (q->state & Qclosed) {
		len = BALLOC(b);
		freeblist(b);
		iunlock(&q->lock);
		return len;
	}

	/* add buffer to queue */
	QDEBUG checkb(b, "qpass");
	while (b) {
		apipe_write(&q->pipe, &b, 1);
		QDEBUG checkb(b, "qpass");
		len += BALLOC(b);
		dlen += BLEN(b);
		b = b->next;
	}
	q->len += len;
	q->dlen += dlen;

	if (q->len >= q->limit / 2)
		q->state |= Qflow;

	if (q->state & Qstarve) {
		q->state &= ~Qstarve;
	}
	iunlock(&q->lock);

	return len;
}

int qpassnolim(struct queue *q, struct block *b)
{
	int dlen = 0, len = 0, dowakeup;

	/* sync with qread */
	dowakeup = 0;
	ilock(&q->lock);

	if (q->state & Qclosed) {
		len = BALLOC(b);
		freeblist(b);
		iunlock(&q->lock);
		return len;
	}

	/* add buffer to queue */
	while (b) {
		apipe_write(&q->pipe, &b, 1);
		QDEBUG checkb(b, "qpassnolim");
		len += BALLOC(b);
		dlen += BLEN(b);
		b = b->next;
	}
	q->len += len;
	q->dlen += dlen;

	if (q->len >= q->limit / 2)
		q->state |= Qflow;

	if (q->state & Qstarve) {
		q->state &= ~Qstarve;
	}
	iunlock(&q->lock);
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

	for (l = &bp; *l; l = &(*l)->next) {
		nbp = *l;
		n = BLEN(nbp);
		if ((n << 2) < BALLOC(nbp)) {
			*l = allocb(n);
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

	/* sync with qread */
	dowakeup = 0;
	ilock(&q->lock);

	/* no waiting receivers, room in buffer? */
	if (q->len >= q->limit) {
		q->state |= Qflow;
		iunlock(&q->lock);
		return -1;
	}

	/* save in buffer */
	b = iallocb(len);
	if (b == 0) {
		iunlock(&q->lock);
		return 0;
	}
	memmove(b->wp, p, len);
	producecnt += len;
	b->wp += len;
	apipe_write(&q->pipe, &b, 1);

	q->len += BALLOC(b);
	q->dlen += BLEN(b);
	QDEBUG checkb(b, "qproduce");

	if (q->state & Qstarve) {
		q->state &= ~Qstarve;
	}

	if (q->len >= q->limit)
		q->state |= Qflow;
	iunlock(&q->lock);

	return len;
}

/*
 *  copy from offset in the queue
 */
struct block *qcopy(struct queue *q, int len, uint32_t offset)
{
	int sofar;
	int n;
	struct block *b, *nb;
	uint8_t *p;

	nb = allocb(len);

	ilock(&q->lock);

	/* go to offset */
	b = qhead(q);
	for (sofar = 0;; sofar += n) {
		if (b == NULL) {
			iunlock(&q->lock);
			return nb;
		}
		n = BLEN(b);
		if (sofar + n > offset) {
			p = b->rp + offset - sofar;
			n -= offset - sofar;
			break;
		}
		QDEBUG checkb(b, "qcopy");
		qremove(q);
		b = qhead(q);
	}

	/* copy bytes from there */
	for (sofar = 0; sofar < len;) {
		if (n > len - sofar)
			n = len - sofar;
		memmove(nb->wp, p, n);
		qcopycnt += n;
		sofar += n;
		nb->wp += n;
		qremove(q);
		b = qhead(q);
		if (b == NULL)
			break;
		n = BLEN(b);
		p = b->rp;
	}
	iunlock(&q->lock);

	return nb;
}

/*
 *  called by non-interrupt code
 */
struct queue *qopen(int limit,
	int msg, void (*kick) (void *), void *arg)
{
	struct queue *q;
	uint8_t *buf;
	int elsize = sizeof(struct block*);

	q = kzmalloc(sizeof(struct queue), KMALLOC_WAIT);
	if (q == 0)
		return 0;

	spinlock_init_irqsave(&q->lock);
	/* These qlocks are unused right now */
	qlock_init(&q->rlock);
	qlock_init(&q->wlock);
	q->limit = q->inilim = limit;
	q->kick = kick;
	q->arg = arg;
	q->state = msg;

	q->state |= Qstarve;
	q->eof = 0;
	q->noblock = 0;
	buf = kzmalloc(limit * elsize, KMALLOC_WAIT);
	apipe_init(&q->pipe, buf, limit, elsize);

	return q;
}

/* open a queue to be bypassed */
struct queue *qbypass(void (*bypass) (void *, struct block *), void *arg)
{
	struct queue *q;

	q = kzmalloc(sizeof(struct queue), 0);
	if (q == 0)
		return 0;

	/* TODO: Assuming non-irqsave for now */
	spinlock_init(&q->lock);
	/* These qlocks are unused right now */
	qlock_init(&q->rlock);
	qlock_init(&q->wlock);
	q->limit = 0;
	q->arg = arg;
	q->bypass = bypass;
	q->state = 0;

	return q;
}

#if 0
static int notempty(void *a)
{
	struct queue *q = a;

	return (q->state & Qclosed) || qhead(q) != 0;
}
#endif

/*
 *  wait for the queue to be non-empty or closed.
 *  called with q ilocked.
 */
static int qwait(struct queue *q)
{
	/* wait for data */
	for (;;) {
		if (qhead(q) != NULL)
			break;

		if (q->state & Qclosed) {
			if (++q->eof > 3)
				return -1;
			if (*q->err && strcmp(q->err, Ehungup) != 0)
				return -1;
			return 0;
		}

		q->state |= Qstarve;	/* flag requesting producer to wake me */
		iunlock(&q->lock);
		/* how do we do this? 
		   sleep(&q->rr, notempty, q);
		 */
		schedule();
		ilock(&q->lock);
	}
	return 1;
}

/*
 * add a block list to a queue. Less efficient than before; 
 * lots of overhead for these function calls.
 */
void qaddlist(struct queue *q, struct block *b)
{
	/* queue the block */
	while (b){
		apipe_write(&q->pipe, &b, 1);
		q->len += blockalloclen(b);
		q->dlen += blocklen(b);
		b = b->next;
	}
}

/*
 *  called with q ilocked?
 */
struct block *qhead(struct queue *q)
{
	struct block **bb = apipe_head(&q->pipe);
	if (! bb)
		return NULL;
	return *bb;
}

/*
 *  called with q ilocked
 */
struct block *qremove(struct queue *q)
{
	struct block *b;

	apipe_read_locked(&q->pipe, &b, 1);
	if (b == NULL)
		return NULL;
	b->next = NULL;
	q->dlen -= BLEN(b);
	q->len -= BALLOC(b);
	//QDEBUG checkb(b, "qremove");
	return b;
}

/*
 *  copy the contents of a string of blocks into
 *  memory.  emptied blocks are freed.  return
 *  pointer to first unconsumed block.
 */
struct block *bl2mem(uint8_t * p, struct block *b, int *pn)
{
	int i;
	struct block *next;
	int n = *pn;
	*pn = 0;

	for (; b != NULL; b = next) {
		i = BLEN(b);
		if (i > n) {
			memmove(p, b->rp, n);
			b->rp += n;
			*pn += n;
			return b;
		}
		memmove(p, b->rp, i);
		*pn += i;
		n -= i;
		p += i;
		b->rp += i;
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
	ERRSTACK(2);
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

		*l = b = allocb(n);
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
 *  flow control, get producer going again
 *  called with q ilocked
 */
static void qwakeup_iunlock(struct queue *q)
{
	int dowakeup;

	/* if writer flow controlled, restart */
	if ((q->state & Qflow) && q->len < q->limit / 2) {
		q->state &= ~Qflow;
		dowakeup = 1;
	} else
		dowakeup = 0;

	iunlock(&q->lock);

	/* wakeup flow controlled writers */
	if (dowakeup) {
		if (q->kick)
			q->kick(q->arg);
		//wakeup(&q->wr);
	}
}

/* intended to be called by apipe_read_cond. This is the only way
 * that qio should read due to the length consideration.
 */
struct blockstatus {
	int blockcount;
	int count;
	int want;
	int nonblock;
	struct block *b, *last;
	struct queue *q;
};

/* we will assume pipe is owned by us. */
/* note: bs->want is a not-to-exceed. But non-zero is considered
 * to satisfy the read.
 */
int readcond(struct atomic_pipe *p, void *v)
{
	struct blockstatus *bs = v;
	struct block *b, *nb;
	int blockcount = 0;
	int n, len;
	/* we'll need a way to know if a pipe is at EOF. */
	while (qhead(bs->q)){
		/* if we get here, there's at least one block in the queue */
		b = qhead(bs->q);
		if (!bs->b){
			bs->b = b;
			bs->last = b;
		}
		n = BLEN(b);
		len = bs->want-bs->count;
		/* split block if it's too big and this is not a message queue */
		nb = b;
		if (n > len) {
			if ((bs->q->state & Qmsg) == 0) {
				n -= len;
				nb = allocb(len);
				memmove(nb->wp, b->rp, len);
				b->wp += n;
			}
			nb->wp = nb->rp + len;
		} else {
			nb = qremove(bs->q);
		}

		bs->last->next = nb;
		bs->last = nb;
		nb->next = NULL;
		bs->count += BLEN(nb);
		blockcount++;
		if (bs->blockcount && (blockcount >= bs->blockcount))
			break;
	}

	if ((bs->nonblock) || (bs->blockcount && blockcount) || (bs->count/* >= bs->want*/)){
		return 1;
	}
	if (blockcount && (bs->q->state & Qmsg))
		return 1;
	return 0;
}

/*
 *  get next block from a queue (up to a limit)
 *  remove lock management and let the pipes do it.
 *  If you want this locked, i.e. multi-element read,
 *  you should be doing the apipe_read_cond. Otherwise
 *  it's quite unsafe.
 */
struct block *qbread(struct queue *q, int len)
{
	struct block *b, *nb;
	int n;
	struct blockstatus bs;
	memset(&bs, 0, sizeof(bs));
	bs.want = len;
	bs.blockcount = 1;
	bs.q = q;
	if (apipe_read_cond(&q->pipe, readcond, &bs) < 0)
		error("qbread: apipe_read_cond failed");
	return bs.b;
}

/*
 *  read a queue.  if no data is queued, post a struct block
 *  and wait on its Rendez.
 */
long qread(struct queue *q, void *vp, int len)
{
	struct block *b, *first, **l;
	int blen, n;
	struct blockstatus bs;
	memset(&bs, 0, sizeof(bs));
	bs.want = len;
	bs.q = q;
	bs.nonblock = 1;
	bs.blockcount = 1;
	if (apipe_read_cond(&q->pipe, readcond, &bs) < 0)
		error("qbread: apipe_read_cond failed");
	bl2mem(vp, bs.b, &len);
	return len;
}

static int qnotfull(void *a)
{
	struct queue *q = a;

	return q->len < q->limit || (q->state & Qclosed);
}

uint32_t noblockcnt;

/*
 *  add a block to a queue obeying flow control
 */
long qbwrite(struct queue *q, struct block *b)
{
	ERRSTACK(2);
	int n, dowakeup;

	n = BLEN(b);

	if (q->bypass) {
		(*q->bypass) (q->arg, b);
		return n;
	}

	dowakeup = 0;
	qlock(&q->wlock);
	if (waserror()) {
		if (b != NULL)
			freeb(b);
		qunlock(&q->wlock);
		nexterror();
	}

	ilock(&q->lock);

	/* give up if the queue is closed */
	if (q->state & Qclosed) {
		iunlock(&q->lock);
		error(q->err);
	}

	/* if nonblocking, don't queue over the limit */
	if (q->len >= q->limit) {
		if (q->noblock) {
			iunlock(&q->lock);
			freeb(b);
			noblockcnt += n;
			qunlock(&q->wlock);
			poperror();
			return n;
		}
	}

	/* queue the block */
	apipe_write(&q->pipe, &b, 1);
	q->len += BALLOC(b);
	q->dlen += n;
	QDEBUG checkb(b, "qbwrite");
	b = NULL;

	/* make sure other end gets awakened */
	if (q->state & Qstarve) {
		q->state &= ~Qstarve;
		/* the write shold have made that happen. */
		dowakeup = 1;
	}
	iunlock(&q->lock);

	/*  get output going again */
	if (q->kick && (dowakeup || (q->state & Qkick)))
		q->kick(q->arg);

	/* wakeup anyone consuming at the other end */
	if (dowakeup) ;	//wakeup(&q->rr);

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
		if (q->noblock || qnotfull(q))
			break;

		ilock(&q->lock);
		q->state |= Qflow;
		iunlock(&q->lock);
		/* how do we do this?
		   sleep(&q->wr, qnotfull, q);
		 */
	}

	qunlock(&q->wlock);
	poperror();
	return n;
}

/*
 *  write to a queue.  only Maxatomic bytes at a time is atomic.
 */
int qwrite(struct queue *q, void *vp, int len)
{
	ERRSTACK(1);
	int n, sofar;
	struct block *b;
	uint8_t *p = vp;

	sofar = 0;
	do {
		n = len - sofar;
		if (n > Maxatomic)
			n = Maxatomic;

		b = allocb(n);
		if (waserror()) {
			freeb(b);
			nexterror();
		}
		memmove(b->wp, p + sofar, n);
		poperror();
		b->wp += n;

		qbwrite(q, b);

		sofar += n;
	} while (sofar < len && (q->state & Qmsg) == 0);

	return len;
}

/*
 *  used by print() to write to a queue.  Since we may be splhi or not in
 *  a process, don't qlock.
 *
 *  this routine merges adjacent blocks if block n+1 will fit into
 *  the free space of block n.
 */
int qiwrite(struct queue *q, void *vp, int len)
{
	int n, sofar, dowakeup;
	struct block *b;
	uint8_t *p = vp;

	dowakeup = 0;

	sofar = 0;
	do {
		n = len - sofar;
		if (n > Maxatomic)
			n = Maxatomic;

		b = iallocb(n);
		if (b == NULL)
			break;
		memmove(b->wp, p + sofar, n);
		b->wp += n;

		ilock(&q->lock);

		/* we use an artificially high limit for kernel prints since anything
		 * over the limit gets dropped
		 */
		if (q->dlen >= 16 * 1024) {
			iunlock(&q->lock);
			freeb(b);
			break;
		}

		QDEBUG checkb(b, "qiwrite");
		apipe_write(&q->pipe, &b, 1);
		q->len += BALLOC(b);
		q->dlen += n;

		if (q->state & Qstarve) {
			q->state &= ~Qstarve;
			dowakeup = 1;
		}

		iunlock(&q->lock);


		if (dowakeup) {
			if (q->kick)
				q->kick(q->arg);
			/* assumption: the write did all the kicking we need? */
		}

		sofar += n;
	} while (sofar < len && (q->state & Qmsg) == 0);

	return sofar;
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
	ilock(&q->lock);
	q->state |= Qclosed;
	q->state &= ~(Qflow | Qstarve);
	strncpy(q->err, Ehungup, sizeof(q->err));
	q->len = 0;
	q->dlen = 0;
	q->noblock = 0;
	iunlock(&q->lock);

	/* free queued blocks */
	while (qhead(q))
		freeb(qremove(q));

	/* wake up readers/writers */
	//wakeup(&q->rr);
	//wakeup(&q->wr);
}

/*
 *  be extremely careful when calling this,
 *  as there is no reference accounting
 */
void qfree(struct queue *q)
{
	qclose(q);
	kfree(q->pipe.ap_buf);
	kfree(q);
}

/*
 *  Mark a queue as closed.  Wakeup any readers.  Don't remove queued
 *  blocks.
 */
void qhangup(struct queue *q, char *msg)
{
	/* mark it */
	ilock(&q->lock);
	q->state |= Qclosed;
	if (msg == 0 || *msg == 0)
		strncpy(q->err, Ehungup, sizeof(q->err));
	else
		strncpy(q->err, msg, sizeof(q->err));
	iunlock(&q->lock);

	/* wake up readers/writers */
	//wakeup(&q->rr);
	//wakeup(&q->wr);
}

/*
 *  return non-zero if the q is hungup
 */
int qisclosed(struct queue *q)
{
	return q->state & Qclosed;
}

/*
 *  mark a queue as no longer hung up
 */
void qreopen(struct queue *q)
{
	ilock(&q->lock);
	q->state &= ~Qclosed;
	q->state |= Qstarve;
	q->eof = 0;
	q->limit = q->inilim;
	iunlock(&q->lock);
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
	return qhead(q) != 0;
}

/*
 *  change queue limit
 */
void qsetlimit(struct queue *q, int limit)
{
	q->limit = limit;
}

/*
 *  set blocking/nonblocking
 */
void qnoblock(struct queue *q, int onoff)
{
	q->noblock = onoff;
}

/*
 *  flush the output queue
 */
void qflush(struct queue *q)
{
	struct block *bfirst;

	/* mark it */
	ilock(&q->lock);
	q->len = 0;
	q->dlen = 0;
	iunlock(&q->lock);

	/* free queued blocks */
	while(qhead(q)){
		freeb(qremove(q));
	}

	/* wake up readers/writers */
	//wakeup(&q->wr);
}

int qfull(struct queue *q)
{
	return q->state & Qflow;
}

int qstate(struct queue *q)
{
	return q->state;
}
