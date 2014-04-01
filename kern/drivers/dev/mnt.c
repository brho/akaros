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

/*
 * References are managed as follows:
 * The channel to the server - a network connection or pipe - has one
 * reference for every Chan open on the server.  The server channel has
 * c->mux set to the Mnt used for muxing control to that server.  Mnts
 * have no reference count; they go away when c goes away.
 * Each channel derived from the mount point has mchan set to c,
 * and increfs/decrefs mchan to manage references on the server
 * connection.
 */

#define MAXRPC (IOHDRSZ+8192)

static __inline int isxdigit(int c)
{
	if ((c >= '0') && (c <= '9'))
		return 1;
	if ((c >= 'a') && (c <= 'f'))
		return 1;
	if ((c >= 'A') && (c <= 'F'))
		return 1;
	return 0;
}

struct mntrpc {
	struct chan *c;				/* Channel for whom we are working */
	struct mntrpc *list;		/* Free/pending list */
	struct fcall request;		/* Outgoing file system protocol message */
	struct fcall reply;			/* Incoming reply */
	struct mnt *m;				/* Mount device during rpc */
	struct rendez r;			/* Place to hang out */
	uint8_t *rpc;				/* I/O Data buffer */
	unsigned int rpclen;		/* len of buffer */
	struct block *b;			/* reply blocks */
	char done;					/* Rpc completed */
	uint64_t stime;				/* start time for mnt statistics */
	uint32_t reqlen;			/* request length for mnt statistics */
	uint32_t replen;			/* reply length for mnt statistics */
	struct mntrpc *flushed;		/* message this one flushes */
};

enum {
	TAGSHIFT = 5,				/* uint32_t has to be 32 bits */
	TAGMASK = (1 << TAGSHIFT) - 1,
	NMASK = (64 * 1024) >> TAGSHIFT,
};

/* Our TRUNC and remove on close differ from 9ps, so we'll need to translate.
 * I got these flags from http://man.cat-v.org/plan_9/5/open */
#define MNT_9P_OPEN_OTRUNC		0x10
#define MNT_9P_OPEN_ORCLOSE		0x40

struct Mntalloc {
	spinlock_t l;
	struct mnt *list;			/* Mount devices in use */
	struct mnt *mntfree;		/* Free list */
	struct mntrpc *rpcfree;
	int nrpcfree;
	int nrpcused;
	uint32_t id;
	uint32_t tagmask[NMASK];
} mntalloc;

void mattach(struct mnt *, struct chan *, char *unused_char_p_t);
struct mnt *mntchk(struct chan *);
void mntdirfix(uint8_t * unused_uint8_p_t, struct chan *);
struct mntrpc *mntflushalloc(struct mntrpc *, uint32_t);
void mntflushfree(struct mnt *, struct mntrpc *);
void mntfree(struct mntrpc *);
void mntgate(struct mnt *);
void mntpntfree(struct mnt *);
void mntqrm(struct mnt *, struct mntrpc *);
struct mntrpc *mntralloc(struct chan *, uint32_t);
long mntrdwr(int unused_int, struct chan *, void *, long, int64_t);
int mntrpcread(struct mnt *, struct mntrpc *);
void mountio(struct mnt *, struct mntrpc *);
void mountmux(struct mnt *, struct mntrpc *);
void mountrpc(struct mnt *, struct mntrpc *);
int rpcattn(void *);
struct chan *mntchan(void);

char Esbadstat[] = "invalid directory entry received from server";
char Enoversion[] = "version not established for mount channel";

void (*mntstats) (int unused_int, struct chan *, uint64_t, uint32_t);

static void mntinit(void)
{
	mntalloc.id = 1;
	mntalloc.tagmask[0] = 1;	/* don't allow 0 as a tag */
	mntalloc.tagmask[NMASK - 1] = 0x80000000UL;	/* don't allow NOTAG */
	//fmtinstall('F', fcallfmt);
/*	fmtinstall('D', dirfmt); */
/*	fmtinstall('M', dirmodefmt);  */

	cinit();
}

/*
 * Version is not multiplexed: message sent only once per connection.
 */
long mntversion(struct chan *c, char *version, int msize, int returnlen)
{
	ERRSTACK(2);
	struct fcall f;
	uint8_t *msg;
	struct mnt *m;
	char *v;
	long k, l;
	uint64_t oo;
	char buf[128];

	qlock(&c->umqlock);	/* make sure no one else does this until we've established ourselves */
	if (waserror()) {
		qunlock(&c->umqlock);
		nexterror();
	}

	/* defaults */
	if (msize == 0)
		msize = MAXRPC;
	if (msize > c->iounit && c->iounit != 0)
		msize = c->iounit;
	v = version;
	if (v == NULL || v[0] == '\0')
		v = VERSION9P;

	/* validity */
	if (msize < 0)
		error("bad iounit in version call");
	if (strncmp(v, VERSION9P, strlen(VERSION9P)) != 0)
		error("bad 9P version specification");

	m = c->mux;

	if (m != NULL) {
		qunlock(&c->umqlock);
		poperror();

		strncpy(buf, m->version, sizeof buf);
		k = strlen(buf);
		if (strncmp(buf, v, k) != 0) {
			snprintf(buf, sizeof buf, "incompatible 9P versions %s %s",
					 m->version, v);
			error(buf);
		}
		if (returnlen > 0) {
			if (returnlen < k)
				error(Eshort);
			memmove(version, buf, k);
		}
		return k;
	}

	f.type = Tversion;
	f.tag = NOTAG;
	f.msize = msize;
	f.version = v;
	msg = kzmalloc(8192 + IOHDRSZ, 0);
	if (msg == NULL)
		exhausted("version memory");
	if (waserror()) {
		kfree(msg);
		nexterror();
	}
	k = convS2M(&f, msg, 8192 + IOHDRSZ);
	if (k == 0)
		error("bad fversion conversion on send");

	spin_lock(&c->lock);
	oo = c->offset;
	c->offset += k;
	spin_unlock(&c->lock);

	l = devtab[c->type].write(c, msg, k, oo);

	if (l < k) {
		spin_lock(&c->lock);
		c->offset -= k - l;
		spin_unlock(&c->lock);
		error("short write in fversion");
	}

	/* message sent; receive and decode reply */
	k = devtab[c->type].read(c, msg, 8192 + IOHDRSZ, c->offset);
	if (k <= 0)
		error("EOF receiving fversion reply");

	spin_lock(&c->lock);
	c->offset += k;
	spin_unlock(&c->lock);

	l = convM2S(msg, k, &f);
	if (l != k)
		error("bad fversion conversion on reply");
	if (f.type != Rversion) {
		if (f.type == Rerror)
			error(f.ename);
		error("unexpected reply type in fversion");
	}
	if (f.msize > msize)
		error("server tries to increase msize in fversion");
	if (f.msize < 256 || f.msize > 1024 * 1024)
		error("nonsense value of msize in fversion");
	if (strncmp(f.version, v, strlen(f.version)) != 0)
		error("bad 9P version returned from server");

	/* now build Mnt associated with this connection */
	spin_lock(&mntalloc.l);
	m = mntalloc.mntfree;
	if (m != 0)
		mntalloc.mntfree = m->list;
	else {
		m = kzmalloc(sizeof(struct mnt), 0);
		if (m == 0) {
			spin_unlock(&mntalloc.l);
			exhausted("mount devices");
		}
	}
	m->list = mntalloc.list;
	mntalloc.list = m;
	m->version = NULL;
	kstrdup(&m->version, f.version);
	m->id = mntalloc.id++;
	m->q = qopen(10 * MAXRPC, 0, NULL, NULL);
	m->msize = f.msize;
	spin_unlock(&mntalloc.l);

	poperror();	/* msg */
	kfree(msg);

	spin_lock(&m->lock);
	m->queue = 0;
	m->rip = 0;

	c->flag |= CMSG;
	c->mux = m;
	m->c = c;
	spin_unlock(&m->lock);

	poperror();	/* c */
	qunlock(&c->umqlock);
	k = strlen(f.version);
	if (returnlen > 0) {
		if (returnlen < k)
			error(Eshort);
		memmove(version, f.version, k);
	}

	return k;
}

struct chan *mntauth(struct chan *c, char *spec)
{
	ERRSTACK(2);
	struct mnt *m;
	struct mntrpc *r;

	m = c->mux;

	if (m == NULL) {
		mntversion(c, VERSION9P, MAXRPC, 0);
		m = c->mux;
		if (m == NULL)
			error(Enoversion);
	}

	c = mntchan();
	if (waserror()) {
		/* Close must not be called since it will
		 * call mnt recursively
		 */
		chanfree(c);
		nexterror();
	}

	r = mntralloc(0, m->msize);

	if (waserror()) {
		mntfree(r);
		nexterror();
	}

	r->request.type = Tauth;
	r->request.afid = c->fid;
	r->request.uname = current->user;
	r->request.aname = spec;
	mountrpc(m, r);

	c->qid = r->reply.aqid;
	c->mchan = m->c;
	kref_get(&m->c->ref, 1);
	c->mqid = c->qid;
	c->mode = ORDWR;

	poperror();	/* r */
	mntfree(r);

	poperror();	/* c */

	return c;

}

static struct chan *mntattach(char *muxattach)
{
	ERRSTACK(2);
	struct mnt *m;
	struct chan *c;
	struct mntrpc *r;
	struct bogus {
		struct chan *chan;
		struct chan *authchan;
		char *spec;
		int flags;
	} bogus;

	bogus = *((struct bogus *)muxattach);
	c = bogus.chan;

	m = c->mux;

	if (m == NULL) {
		mntversion(c, NULL, 0, 0);
		m = c->mux;
		if (m == NULL)
			error(Enoversion);
	}

	c = mntchan();
	if (waserror()) {
		/* Close must not be called since it will
		 * call mnt recursively
		 */
		chanfree(c);
		nexterror();
	}

	r = mntralloc(0, m->msize);

	if (waserror()) {
		mntfree(r);
		nexterror();
	}

	r->request.type = Tattach;
	r->request.fid = c->fid;
	if (bogus.authchan == NULL)
		r->request.afid = NOFID;
	else
		r->request.afid = bogus.authchan->fid;
	r->request.uname = current->user;
	r->request.aname = bogus.spec;
	mountrpc(m, r);

	c->qid = r->reply.qid;
	c->mchan = m->c;
	kref_get(&m->c->ref, 1);
	c->mqid = c->qid;

	poperror();	/* r */
	mntfree(r);

	poperror();	/* c */

	if (bogus.flags & MCACHE)
		c->flag |= CCACHE;
	return c;
}

struct chan *mntchan(void)
{
	struct chan *c;

	c = devattach('M', 0);
	spin_lock(&mntalloc.l);
	c->dev = mntalloc.id++;
	spin_unlock(&mntalloc.l);

	if (c->mchan)
		panic("mntchan non-zero %p", c->mchan);
	return c;
}

static struct walkqid *mntwalk(struct chan *c, struct chan *nc, char **name,
							   int nname)
{
	ERRSTACK(2);
	volatile int alloc;
	int i;
	struct mnt *m;
	struct mntrpc *r;
	struct walkqid *wq;

	if (nc != NULL)
		printd("mntwalk: nc != NULL\n");
	if (nname > MAXWELEM)
		error("devmnt: too many name elements");
	alloc = 0;
	wq = kzmalloc(sizeof(struct walkqid) + nname * sizeof(struct qid),
				  KMALLOC_WAIT);
	if (waserror()) {
		if (alloc && wq->clone != NULL)
			cclose(wq->clone);
		kfree(wq);
		poperror();
		return NULL;
	}

	alloc = 0;
	m = mntchk(c);
	r = mntralloc(c, m->msize);
	if (nc == NULL) {
		nc = devclone(c);
		/* Until the other side accepts this fid, we can't mntclose it.
		 * Therefore set type to -1 for now.  inferno was setting this to 0,
		 * assuming it was devroot.  lining up with chanrelease and newchan */
		nc->type = -1;
		alloc = 1;
	}
	wq->clone = nc;

	if (waserror()) {
		mntfree(r);
		nexterror();
	}
	r->request.type = Twalk;
	r->request.fid = c->fid;
	r->request.newfid = nc->fid;
	r->request.nwname = nname;
	memmove(r->request.wname, name, nname * sizeof(char *));

	mountrpc(m, r);

	if (r->reply.nwqid > nname)
		error("too many QIDs returned by walk");
	if (r->reply.nwqid < nname) {
		if (alloc)
			cclose(nc);
		wq->clone = NULL;
		if (r->reply.nwqid == 0) {
			kfree(wq);
			wq = NULL;
			goto Return;
		}
	}

	/* move new fid onto mnt device and update its qid */
	if (wq->clone != NULL) {
		if (wq->clone != c) {
			wq->clone->type = c->type;
			wq->clone->mchan = c->mchan;
			kref_get(&c->mchan->ref, 1);
		}
		if (r->reply.nwqid > 0)
			wq->clone->qid = r->reply.wqid[r->reply.nwqid - 1];
	}
	wq->nqid = r->reply.nwqid;
	for (i = 0; i < wq->nqid; i++)
		wq->qid[i] = r->reply.wqid[i];

Return:
	poperror();
	mntfree(r);
	poperror();
	return wq;
}

static int mntstat(struct chan *c, uint8_t * dp, int n)
{
	ERRSTACK(1);
	struct mnt *m;
	struct mntrpc *r;

	if (n < BIT16SZ)
		error(Eshortstat);
	m = mntchk(c);
	r = mntralloc(c, m->msize);
	if (waserror()) {
		mntfree(r);
		nexterror();
	}
	r->request.type = Tstat;
	r->request.fid = c->fid;
	mountrpc(m, r);

	if (r->reply.nstat > n) {
		/* doesn't fit; just patch the count and return */
		PBIT16((uint8_t *) dp, r->reply.nstat);
		n = BIT16SZ;
	} else {
		n = r->reply.nstat;
		memmove(dp, r->reply.stat, n);
		validstat(dp, n);
		mntdirfix(dp, c);
	}
	poperror();
	mntfree(r);
	return n;
}

static struct chan *mntopencreate(int type, struct chan *c, char *name,
								  int omode, uint32_t perm)
{
	ERRSTACK(1);
	struct mnt *m;
	struct mntrpc *r;

	m = mntchk(c);
	r = mntralloc(c, m->msize);
	if (waserror()) {
		mntfree(r);
		nexterror();
	}
	r->request.type = type;
	r->request.fid = c->fid;
	r->request.mode = omode & O_ACCMODE;	/* not using openmode, want O_EXEC */
	if (omode & O_TRUNC)
		r->request.mode |= MNT_9P_OPEN_OTRUNC;
	if (omode & O_REMCLO)
		r->request.mode |= MNT_9P_OPEN_ORCLOSE;
	if (type == Tcreate) {
		r->request.perm = perm;
		r->request.name = name;
	}
	mountrpc(m, r);

	c->qid = r->reply.qid;
	c->offset = 0;
	c->mode = openmode(omode);
	c->iounit = r->reply.iounit;
	if (c->iounit == 0 || c->iounit > m->msize - IOHDRSZ)
		c->iounit = m->msize - IOHDRSZ;
	c->flag |= COPEN;
	poperror();
	mntfree(r);

	if (c->flag & CCACHE)
		copen(c);

	return c;
}

static struct chan *mntopen(struct chan *c, int omode)
{
	return mntopencreate(Topen, c, NULL, omode, 0);
}

static void mntcreate(struct chan *c, char *name, int omode, uint32_t perm)
{
	mntopencreate(Tcreate, c, name, omode, perm);
}

static void mntclunk(struct chan *c, int t)
{
	ERRSTACK(1);
	struct mnt *m;
	struct mntrpc *r;

	m = mntchk(c);
	r = mntralloc(c, m->msize);
	if (waserror()) {
		mntfree(r);
		nexterror();
	}

	r->request.type = t;
	r->request.fid = c->fid;
	mountrpc(m, r);
	mntfree(r);
	poperror();
}

void muxclose(struct mnt *m)
{
	struct mntrpc *q, *r;

	for (q = m->queue; q; q = r) {
		r = q->list;
		mntfree(q);
	}
	m->id = 0;
	kfree(m->version);
	m->version = NULL;
	mntpntfree(m);
}

void mntpntfree(struct mnt *m)
{
	struct mnt *f, **l;
	struct queue *q;

	spin_lock(&mntalloc.l);
	l = &mntalloc.list;
	for (f = *l; f; f = f->list) {
		if (f == m) {
			*l = m->list;
			break;
		}
		l = &f->list;
	}
	m->list = mntalloc.mntfree;
	mntalloc.mntfree = m;
	q = m->q;
	spin_unlock(&mntalloc.l);

	qfree(q);
}

static void mntclose(struct chan *c)
{
	mntclunk(c, Tclunk);
}

static void mntremove(struct chan *c)
{
	mntclunk(c, Tremove);
}

static int mntwstat(struct chan *c, uint8_t * dp, int n)
{
	ERRSTACK(1);
	struct mnt *m;
	struct mntrpc *r;

	m = mntchk(c);
	r = mntralloc(c, m->msize);
	if (waserror()) {
		mntfree(r);
		nexterror();
	}
	r->request.type = Twstat;
	r->request.fid = c->fid;
	r->request.nstat = n;
	r->request.stat = dp;
	mountrpc(m, r);
	poperror();
	mntfree(r);
	return n;
}

static long mntread(struct chan *c, void *buf, long n, int64_t off)
{
	uint8_t *p, *e;
	int nc, cache, isdir, dirlen;

	isdir = 0;
	cache = c->flag & CCACHE;
	if (c->qid.type & QTDIR) {
		cache = 0;
		isdir = 1;
	}

	p = buf;
	if (cache) {
		nc = cread(c, buf, n, off);
		if (nc > 0) {
			n -= nc;
			if (n == 0)
				return nc;
			p += nc;
			off += nc;
		}
		n = mntrdwr(Tread, c, p, n, off);
		cupdate(c, p, n, off);
		return n + nc;
	}

	n = mntrdwr(Tread, c, buf, n, off);
	if (isdir) {
		for (e = &p[n]; p + BIT16SZ < e; p += dirlen) {
			dirlen = BIT16SZ + GBIT16(p);
			if (p + dirlen > e)
				break;
			validstat(p, dirlen);
			mntdirfix(p, c);
		}
		if (p != e)
			error(Esbadstat);
	}
	return n;
}

static long mntwrite(struct chan *c, void *buf, long n, int64_t off)
{
	return mntrdwr(Twrite, c, buf, n, off);
}

long mntrdwr(int type, struct chan *c, void *buf, long n, int64_t off)
{
	ERRSTACK(1);
	struct mnt *m;
	struct mntrpc *r;			/* TO DO: volatile struct { Mntrpc *r; } r; */
	char *uba;
	int cache;
	uint32_t cnt, nr, nreq;

	m = mntchk(c);
	uba = buf;
	cnt = 0;
	cache = c->flag & CCACHE;
	if (c->qid.type & QTDIR)
		cache = 0;
	for (;;) {
		r = mntralloc(c, m->msize);
		if (waserror()) {
			mntfree(r);
			nexterror();
		}
		r->request.type = type;
		r->request.fid = c->fid;
		r->request.offset = off;
		r->request.data = uba;
		nr = n;
		if (nr > m->msize - IOHDRSZ)
			nr = m->msize - IOHDRSZ;
		r->request.count = nr;
		mountrpc(m, r);
		nreq = r->request.count;
		nr = r->reply.count;
		if (nr > nreq)
			nr = nreq;

		if (type == Tread)
			r->b = bl2mem((uint8_t *) uba, r->b, nr);
		else if (cache)
			cwrite(c, (uint8_t *) uba, nr, off);

		poperror();
		mntfree(r);
		off += nr;
		uba += nr;
		cnt += nr;
		n -= nr;
		if (nr != nreq || n == 0 /*|| current->killed */ )
			break;
	}
	return cnt;
}

void mountrpc(struct mnt *m, struct mntrpc *r)
{
	char *sn, *cn;
	int t;
	char *e;

	r->reply.tag = 0;
	r->reply.type = Tmax;	/* can't ever be a valid message type */

	mountio(m, r);

	t = r->reply.type;
	switch (t) {
		case Rerror:
			/* in Akaros mode, first four characters
			 * are errno.
			 */
			e = r->reply.ename;
			/* If it is in the format "XXXX <at least one char>" */
			if ((strlen(e) > 5) && isxdigit(e[0]) &&
				isxdigit(e[1]) &&
				isxdigit(e[2]) &&
				isxdigit(e[3])) {
				int errno = strtoul(e, NULL, 16);
				set_errno(errno);
				error(&r->reply.ename[5]);
			} else
				error(r->reply.ename);
		case Rflush:
			error(Eintr);
		default:
			if (t == r->request.type + 1)
				break;
			sn = "?";
			if (m->c->name != NULL)
				sn = m->c->name->s;
			cn = "?";
			if (r->c != NULL && r->c->name != NULL)
				cn = r->c->name->s;
			printd
				("mnt: proc %s %lu: mismatch from %s %s rep 0x%p tag %d fid %d T%d R%d rp %d\n",
				 "current->text", "current->pid", sn, cn, r, r->request.tag,
				 r->request.fid, r->request.type, r->reply.type, r->reply.tag);
			error(Emountrpc);
	}
}

void mountio(struct mnt *m, struct mntrpc *r)
{
	ERRSTACK(1);
	int n;

	while (waserror()) {
		if (m->rip == current)
			mntgate(m);
		if (strcmp(current_errstr(), Eintr) != 0) {
			mntflushfree(m, r);
			nexterror();
		}
		r = mntflushalloc(r, m->msize);
		/* need one for every waserror call (so this plus one outside) */
		poperror();
	}

	spin_lock(&m->lock);
	r->m = m;
	r->list = m->queue;
	m->queue = r;
	spin_unlock(&m->lock);

	/* Transmit a file system rpc */
	if (m->msize == 0)
		panic("msize");
	n = convS2M(&r->request, r->rpc, m->msize);
	if (n < 0)
		panic("bad message type in mountio");
	if (devtab[m->c->type].write(m->c, r->rpc, n, 0) != n)
		error(Emountrpc);
/*	r->stime = fastticks(NULL); */
	r->reqlen = n;

	/* Gate readers onto the mount point one at a time */
	for (;;) {
		spin_lock(&m->lock);
		if (m->rip == 0)
			break;
		spin_unlock(&m->lock);
		rendez_sleep(&r->r, rpcattn, r);
		if (r->done) {
			poperror();
			mntflushfree(m, r);
			return;
		}
	}
	m->rip = current;
	spin_unlock(&m->lock);
	while (r->done == 0) {
		if (mntrpcread(m, r) < 0)
			error(Emountrpc);
		mountmux(m, r);
	}
	mntgate(m);
	poperror();
	mntflushfree(m, r);
}

static int doread(struct mnt *m, int len)
{
	struct block *b;

	while (qlen(m->q) < len) {
		b = devtab[m->c->type].bread(m->c, m->msize, 0);
		if (b == NULL)
			return -1;
		if (blocklen(b) == 0) {
			freeblist(b);
			return -1;
		}
		qaddlist(m->q, b);
	}
	return 0;
}

int mntrpcread(struct mnt *m, struct mntrpc *r)
{
	int i, t, len, hlen;
	struct block *b, **l, *nb;

	r->reply.type = 0;
	r->reply.tag = 0;

	/* read at least length, type, and tag and pullup to a single block */
	if (doread(m, BIT32SZ + BIT8SZ + BIT16SZ) < 0)
		return -1;
	nb = pullupqueue(m->q, BIT32SZ + BIT8SZ + BIT16SZ);

	/* read in the rest of the message, avoid ridiculous (for now) message sizes */
	len = GBIT32(nb->rp);
	if (len > m->msize) {
		qdiscard(m->q, qlen(m->q));
		return -1;
	}
	if (doread(m, len) < 0)
		return -1;

	/* pullup the header (i.e. everything except data) */
	t = nb->rp[BIT32SZ];
	switch (t) {
		case Rread:
			hlen = BIT32SZ + BIT8SZ + BIT16SZ + BIT32SZ;
			break;
		default:
			hlen = len;
			break;
	}
	nb = pullupqueue(m->q, hlen);

	if (convM2S(nb->rp, len, &r->reply) <= 0) {
		/* bad message, dump it */
		printd("mntrpcread: convM2S failed\n");
		qdiscard(m->q, len);
		return -1;
	}

	/* hang the data off of the fcall struct */
	l = &r->b;
	*l = NULL;
	do {
		b = qremove(m->q);
		if (hlen > 0) {
			b->rp += hlen;
			len -= hlen;
			hlen = 0;
		}
		i = BLEN(b);
		if (i <= len) {
			len -= i;
			*l = b;
			l = &(b->next);
		} else {
			/* split block and put unused bit back */
			nb = allocb(i - len);
			memmove(nb->wp, b->rp + len, i - len);
			b->wp = b->rp + len;
			nb->wp += i - len;
			qputback(m->q, nb);
			*l = b;
			return 0;
		}
	} while (len > 0);

	return 0;
}

void mntgate(struct mnt *m)
{
	struct mntrpc *q;

	spin_lock(&m->lock);
	m->rip = 0;
	for (q = m->queue; q; q = q->list) {
		if (q->done == 0)
			if (rendez_wakeup(&q->r))
				break;
	}
	spin_unlock(&m->lock);
}

void mountmux(struct mnt *m, struct mntrpc *r)
{
	struct mntrpc **l, *q;

	spin_lock(&m->lock);
	l = &m->queue;
	for (q = *l; q; q = q->list) {
		/* look for a reply to a message */
		if (q->request.tag == r->reply.tag) {
			*l = q->list;
			if (q != r) {
				/*
				 * Completed someone else.
				 * Trade pointers to receive buffer.
				 */
				q->reply = r->reply;
				q->b = r->b;
				r->b = NULL;
			}
			q->done = 1;
			spin_unlock(&m->lock);
			if (mntstats != NULL)
				(*mntstats) (q->request.type,
							 m->c, q->stime, q->reqlen + r->replen);
			if (q != r)
				rendez_wakeup(&q->r);
			return;
		}
		l = &q->list;
	}
	spin_unlock(&m->lock);
	if (r->reply.type == Rerror) {
		printd("unexpected reply tag %u; type %d (error %q)\n", r->reply.tag,
			   r->reply.type, r->reply.ename);
	} else {
		printd("unexpected reply tag %u; type %d\n", r->reply.tag,
			   r->reply.type);
	}
}

/*
 * Create a new flush request and chain the previous
 * requests from it
 */
struct mntrpc *mntflushalloc(struct mntrpc *r, uint32_t iounit)
{
	struct mntrpc *fr;

	fr = mntralloc(0, iounit);

	fr->request.type = Tflush;
	if (r->request.type == Tflush)
		fr->request.oldtag = r->request.oldtag;
	else
		fr->request.oldtag = r->request.tag;
	fr->flushed = r;

	return fr;
}

/*
 *  Free a chain of flushes.  Remove each unanswered
 *  flush and the original message from the unanswered
 *  request queue.  Mark the original message as done
 *  and if it hasn't been answered set the reply to to
 *  Rflush.
 */
void mntflushfree(struct mnt *m, struct mntrpc *r)
{
	struct mntrpc *fr;

	while (r) {
		fr = r->flushed;
		if (!r->done) {
			r->reply.type = Rflush;
			mntqrm(m, r);
		}
		if (fr)
			mntfree(r);
		r = fr;
	}
}

static int alloctag(void)
{
	int i, j;
	uint32_t v;

	for (i = 0; i < NMASK; i++) {
		v = mntalloc.tagmask[i];
		if (v == ~0UL)
			continue;
		for (j = 0; j < 1 << TAGSHIFT; j++)
			if ((v & (1 << j)) == 0) {
				mntalloc.tagmask[i] |= 1 << j;
				return (i << TAGSHIFT) + j;
			}
	}
	/* panic("no devmnt tags left"); */
	return NOTAG;
}

static void freetag(int t)
{
	mntalloc.tagmask[t >> TAGSHIFT] &= ~(1 << (t & TAGMASK));
}

struct mntrpc *mntralloc(struct chan *c, uint32_t msize)
{
	struct mntrpc *new;

	spin_lock(&mntalloc.l);
	new = mntalloc.rpcfree;
	if (new == NULL) {
		new = kzmalloc(sizeof(struct mntrpc), 0);
		if (new == NULL) {
			spin_unlock(&mntalloc.l);
			exhausted("mount rpc header");
		}
		rendez_init(&new->r);
		/*
		 * The header is split from the data buffer as
		 * mountmux may swap the buffer with another header.
		 */
		new->rpc = kzmalloc(msize, KMALLOC_WAIT);
		if (new->rpc == NULL) {
			kfree(new);
			spin_unlock(&mntalloc.l);
			exhausted("mount rpc buffer");
		}
		new->rpclen = msize;
		new->request.tag = alloctag();
		if (new->request.tag == NOTAG) {
			kfree(new);
			spin_unlock(&mntalloc.l);
			exhausted("rpc tags");
		}
	} else {
		mntalloc.rpcfree = new->list;
		mntalloc.nrpcfree--;
		if (new->rpclen < msize) {
			kfree(new->rpc);
			new->rpc = kzmalloc(msize, KMALLOC_WAIT);
			if (new->rpc == NULL) {
				kfree(new);
				mntalloc.nrpcused--;
				spin_unlock(&mntalloc.l);
				exhausted("mount rpc buffer");
			}
			new->rpclen = msize;
		}
	}
	mntalloc.nrpcused++;
	spin_unlock(&mntalloc.l);
	new->c = c;
	new->done = 0;
	new->flushed = NULL;
	new->b = NULL;
	return new;
}

void mntfree(struct mntrpc *r)
{
	if (r->b != NULL)
		freeblist(r->b);
	spin_lock(&mntalloc.l);
	if (mntalloc.nrpcfree >= 10) {
		kfree(r->rpc);
		freetag(r->request.tag);
		kfree(r);
	} else {
		r->list = mntalloc.rpcfree;
		mntalloc.rpcfree = r;
		mntalloc.nrpcfree++;
	}
	mntalloc.nrpcused--;
	spin_unlock(&mntalloc.l);
}

void mntqrm(struct mnt *m, struct mntrpc *r)
{
	struct mntrpc **l, *f;

	spin_lock(&m->lock);
	r->done = 1;

	l = &m->queue;
	for (f = *l; f; f = f->list) {
		if (f == r) {
			*l = r->list;
			break;
		}
		l = &f->list;
	}
	spin_unlock(&m->lock);
}

struct mnt *mntchk(struct chan *c)
{
	struct mnt *m;

	/* This routine is mostly vestiges of prior lives; now it's just sanity checking */

	if (c->mchan == NULL)
		panic("mntchk 1: NULL mchan c %s\n", /*c2name(c) */ "channame?");

	m = c->mchan->mux;

	if (m == NULL)
		printd("mntchk 2: NULL mux c %s c->mchan %s \n", c2name(c),
			   c2name(c->mchan));

	/*
	 * Was it closed and reused (was error(Eshutdown); now, it can't happen)
	 */
	if (m->id == 0 || m->id >= c->dev)
		panic("mntchk 3: can't happen");

	return m;
}

/*
 * Rewrite channel type and dev for in-flight data to
 * reflect local values.  These entries are known to be
 * the first two in the Dir encoding after the count.
 */
void mntdirfix(uint8_t * dirbuf, struct chan *c)
{
	unsigned int r;

	r = devtab[c->type].dc;
	dirbuf += BIT16SZ;	/* skip count */
	PBIT16(dirbuf, r);
	dirbuf += BIT16SZ;
	PBIT32(dirbuf, c->dev);
}

int rpcattn(void *v)
{
	struct mntrpc *r;

	r = v;
	return r->done || r->m->rip == 0;
}

struct dev mntdevtab __devtab = {
	'M',
	"mnt",

	devreset,
	mntinit,
	devshutdown,
	mntattach,
	mntwalk,
	mntstat,
	mntopen,
	mntcreate,
	mntclose,
	mntread,
	devbread,
	mntwrite,
	devbwrite,
	mntremove,
	mntwstat,
	devpower,
	devchaninfo,
};
