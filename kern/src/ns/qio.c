// INFERNO
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
	int iNULLim;				/* initial limit */
	int state;
	int noblock;				/* true if writes return immediately when q full */
	int eof;					/* number of eofs read by user */

	void (*kick) (void *);		/* restart output */
	void (*bypass) (void *, struct block *);	/* bypass queue altogether */
	void *arg;					/* argument to kick */

	qlock_t rlock;				/* mutex for reading processes */
	struct rendez rr;			/* process waiting to read */
	qlock_t wlock;				/* mutex for writing processes */
	struct rendez wr;			/* process waiting to write */

	char err[ERRMAX];
};

enum {
	Maxatomic = 64 * 1024,
};

unsigned int qiomaxatomic = Maxatomic;

void ixsummary(void)
{
	debugging ^= 1;
	iallocsummary();
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

	QDEBUG checkb(bp, "padblock 1");
	if (size >= 0) {
		if (bp->rp - bp->base >= size) {
			bp->rp -= size;
			return bp;
		}

		if (bp->next)
			panic("padblock %p", getcallerpc(&bp));
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
			panic("padblock %p", getcallerpc(&bp));

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
 *  make sure the first block has at least n bytes
 */
struct block *pullupqueue(struct queue *q, int n)
{
	struct block *b;

	if ((BLEN(q->bfirst) >= n))
		return q->bfirst;
	q->bfirst = pullupblock(q->bfirst, n);
	for (b = q->bfirst; b != NULL && b->next != NULL; b = b->next) ;
	q->blast = b;
	return q->bfirst;
}

/*
 *  trim to len bytes starting at offset
 */
struct block *trimblock(struct block *bp, int offset, int len)
{
	uint32_t l;
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

	return b;
}

/*
 *  throw away the next 'len' bytes in the queue
 * returning the number actually discarded
 */
int qdiscard(struct queue *q, int len)
{
	struct block *b;
	int dowakeup, n, sofar;

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
	 */
	if ((q->state & Qflow) && q->len < q->limit) {
		q->state &= ~Qflow;
		dowakeup = 1;
	} else
		dowakeup = 0;

	spin_unlock_irqsave(&q->lock);

	if (dowakeup)
		rendez_wakeup(&q->wr);

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

	if (tofree != NULL)
		freeblist(tofree);

	return len;
}

int qpass(struct queue *q, struct block *b)
{
	int dlen, len, dowakeup;

	/* sync with qread */
	dowakeup = 0;
	spin_lock_irqsave(&q->lock);
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

	return len;
}

int qpassnolim(struct queue *q, struct block *b)
{
	int dlen, len, dowakeup;

	/* sync with qread */
	dowakeup = 0;
	spin_lock_irqsave(&q->lock);

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
	spin_lock_irqsave(&q->lock);

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
		b = iallocb(len);
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

static void qinit_common(struct queue *q)
{
	spinlock_init_irqsave(&q->lock);
	qlock_init(&q->rlock);
	qlock_init(&q->wlock);
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

	q->limit = q->iNULLim = limit;
	q->kick = kick;
	q->arg = arg;
	q->state = msg;
	q->state |= Qstarve;
	q->eof = 0;
	q->noblock = 0;

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

/*
 *  wait for the queue to be non-empty or closed.
 *  called with q ilocked.
 */
static int qwait(struct queue *q)
{
	/* wait for data */
	for (;;) {
		if (q->bfirst != NULL)
			break;

		if (q->state & Qclosed) {
			if (++q->eof > 3)
				return -1;
			if (*q->err && strcmp(q->err, Ehungup) != 0)
				return -1;
			return 0;
		}

		q->state |= Qstarve;	/* flag requesting producer to wake me */
		spin_unlock_irqsave(&q->lock);
		rendez_sleep(&q->rr, notempty, q);
		spin_lock_irqsave(&q->lock);
	}
	return 1;
}

/*
 * add a block list to a queue
 */
void qaddlist(struct queue *q, struct block *b)
{
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

/*
 *  copy the contents of a string of blocks into
 *  memory.  emptied blocks are freed.  return
 *  pointer to first unconsumed block.
 */
struct block *bl2mem(uint8_t * p, struct block *b, int n)
{
	int i;
	struct block *next;

	for (; b != NULL; b = next) {
		i = BLEN(b);
		if (i > n) {
			memmove(p, b->rp, n);
			b->rp += n;
			return b;
		}
		memmove(p, b->rp, i);
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
}

/*
 *  get next block from a queue (up to a limit)
 */
struct block *qbread(struct queue *q, int len)
{
	ERRSTACK(1);
	struct block *b, *nb;
	int n;

	qlock(&q->rlock);
	if (waserror()) {
		qunlock(&q->rlock);
		nexterror();
	}

	spin_lock_irqsave(&q->lock);
	switch (qwait(q)) {
		case 0:
			/* queue closed */
			spin_unlock_irqsave(&q->lock);
			qunlock(&q->rlock);
			poperror();
			return NULL;
		case -1:
			/* multiple reads on a closed queue */
			spin_unlock_irqsave(&q->lock);
			error(q->err);
	}

	/* if we get here, there's at least one block in the queue */
	b = qremove(q);
	n = BLEN(b);

	/* split block if it's too big and this is not a message queue */
	nb = b;
	if (n > len) {
		if ((q->state & Qmsg) == 0) {
			n -= len;
			b = allocb(n);
			memmove(b->wp, nb->rp + len, n);
			b->wp += n;
			qputback(q, b);
		}
		nb->wp = nb->rp + len;
	}

	/* restart producer */
	qwakeup_iunlock(q);

	poperror();
	qunlock(&q->rlock);
	return nb;
}

/*
 *  read a queue.  if no data is queued, post a struct block
 *  and wait on its Rendez.
 */
long qread(struct queue *q, void *vp, int len)
{
	ERRSTACK(1);
	struct block *b, *first, **l;
	int m, n;

	qlock(&q->rlock);
	if (waserror()) {
		qunlock(&q->rlock);
		nexterror();
	}

	spin_lock_irqsave(&q->lock);
again:
	switch (qwait(q)) {
		case 0:
			/* queue closed */
			spin_unlock_irqsave(&q->lock);
			qunlock(&q->rlock);
			poperror();
			return 0;
		case -1:
			/* multiple reads on a closed queue */
			spin_unlock_irqsave(&q->lock);
			error(q->err);
	}

	/* if we get here, there's at least one block in the queue */
	// TODO: Consider removing the Qcoalesce flag and force a coalescing
	// strategy by default.
	if (q->state & Qcoalesce) {
		/* when coalescing, 0 length blocks just go away */
		b = q->bfirst;
		if (BLEN(b) <= 0) {
			freeb(qremove(q));
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

	/* copy to user space outside of the ilock */
	spin_unlock_irqsave(&q->lock);
	b = bl2mem(vp, first, len);
	spin_lock_irqsave(&q->lock);

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

	poperror();
	qunlock(&q->rlock);
	return n;
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
	ERRSTACK(1);
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

	spin_lock_irqsave(&q->lock);

	/* give up if the queue is closed */
	if (q->state & Qclosed) {
		spin_unlock_irqsave(&q->lock);
		error(q->err);
	}

	/* if nonblocking, don't queue over the limit */
	if (q->len >= q->limit) {
		if (q->noblock) {
			spin_unlock_irqsave(&q->lock);
			freeb(b);
			noblockcnt += n;
			qunlock(&q->wlock);
			poperror();
			return n;
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

		spin_lock_irqsave(&q->lock);
		q->state |= Qflow;
		spin_unlock_irqsave(&q->lock);
		rendez_sleep(&q->wr, qnotfull, q);
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

	QDEBUG if (!islo())
		 printd("qwrite hi %p\n", getcallerpc(&q));

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

		spin_lock_irqsave(&q->lock);

		QDEBUG checkb(b, "qiwrite");
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
	q->state &= ~(Qflow | Qstarve);
	strncpy(q->err, Ehungup, sizeof(q->err));
	bfirst = q->bfirst;
	q->bfirst = 0;
	q->len = 0;
	q->dlen = 0;
	q->noblock = 0;
	spin_unlock_irqsave(&q->lock);

	/* free queued blocks */
	freeblist(bfirst);

	/* wake up readers/writers */
	rendez_wakeup(&q->rr);
	rendez_wakeup(&q->wr);
}

/*
 *  Mark a queue as closed.  Wakeup any readers.  Don't remove queued
 *  blocks.
 */
void qhangup(struct queue *q, char *msg)
{
	/* mark it */
	spin_lock_irqsave(&q->lock);
	q->state |= Qclosed;
	if (msg == 0 || *msg == 0)
		strncpy(q->err, Ehungup, sizeof(q->err));
	else
		strncpy(q->err, msg, ERRMAX - 1);
	spin_unlock_irqsave(&q->lock);

	/* wake up readers/writers */
	rendez_wakeup(&q->rr);
	rendez_wakeup(&q->wr);
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
	spin_lock_irqsave(&q->lock);
	q->state &= ~Qclosed;
	q->state |= Qstarve;
	q->eof = 0;
	q->limit = q->iNULLim;
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
	spin_lock_irqsave(&q->lock);
	bfirst = q->bfirst;
	q->bfirst = 0;
	q->len = 0;
	q->dlen = 0;
	spin_unlock_irqsave(&q->lock);

	/* free queued blocks */
	freeblist(bfirst);

	/* wake up readers/writers */
	rendez_wakeup(&q->wr);
}

int qfull(struct queue *q)
{
	return q->state & Qflow;
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
