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
#include <fcall.h>

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

/* practical data limit: maxioatomic - IOHDRSZ */
#define MAXRPC (IOHDRSZ+6*8192)

struct mntrpc
{
	struct chan*	c;		/* Channel for whom we are working */
	struct mntrpc*	list;		/* Free/pending list */
	struct fcall	request;	/* Outgoing file system protocol message */
	struct fcall 	reply;		/* Incoming reply */
	struct mnt*	m;		/* Mount device during rpc */
	struct rendez	r;		/* Place to hang out */
	uint8_t*	rpc;		/* I/O Data buffer */
	unsigned int	rpclen;		/* len of buffer */
	struct block	*b;		/* reply blocks */
	char	done;		/* Rpc completed */
	uint64_t	stime;		/* start time for mnt statistics */
	uint32_t	reqlen;		/* request length for mnt statistics */
	uint32_t	replen;		/* reply length for mnt statistics */
	struct mntrpc*	flushed;	/* message this one flushes */
};

enum
{
	TAGSHIFT = 5,			/* uint32_t has to be 32 bits */
	TAGMASK = (1<<TAGSHIFT)-1,
	NMASK = (64*1024)>>TAGSHIFT,
};

struct mntalloc
{
	spinlock_t lock;
	struct mnt*	list;		/* Mount devices in use */
	struct mnt*	mntfree;	/* Free list */
	struct mntrpc*	rpcfree;
	int	nrpcfree;
	int	nrpcused;
	unsigned int	id;
	uint32_t	tagmask[NMASK];
}mntalloc;

struct mnt*	mntchk(struct chan*);
void	mntdirfix( uint8_t *unused_uint8_p_t, struct chan*);
struct mntrpc*	mntflushalloc(struct mntrpc*, uint32_t);
void	mntflushfree(struct mnt*, struct mntrpc*);
void	mntfree(struct mntrpc*);
void	mntgate(struct mnt*);
void	mntpntfree(struct mnt*);
void	mntqrm(struct mnt*, struct mntrpc*);
struct mntrpc*	mntralloc(struct chan*, uint32_t);
long	mntrdwr( int unused_int, struct chan*, void*, long, int64_t);
int	mntrpcread(struct mnt*, struct mntrpc*);
void	mountio(struct mnt*, struct mntrpc*);
void	mountmux(struct mnt*, struct mntrpc*);
void	mountrpc(struct mnt*, struct mntrpc*);
int	rpcattn(void*);
struct chan*	mntchan(void);

char	Esbadstat[] = "invalid directory entry received from server";
char	Enoversion[] = "version not established for mount channel";


void (*mntstats)( int unused_int, struct chan*, uint64_t, uint32_t);

static void
mntreset(void)
{
	mntalloc.id = 1;
	mntalloc.tagmask[0] = 1;			/* don't allow 0 as a tag */
	mntalloc.tagmask[NMASK-1] = 0x80000000UL;	/* don't allow NOTAG *//*
	fmtinstall('F', fcallfmt);
	fmtinstall('D', dirfmt);
									       */
/* We can't install %M since eipfmt does and is used in the kernel [sape] */
/*
	if(mfcinit != NULL)
		mfcinit();
*/
}

/*
 * Version is not multiplexed: message sent only once per connection.
 */
unsigned long
mntversion(struct chan *c, uint32_t msize, char *version,
	   unsigned long returnlen)
{
	ERRSTACK(2);
	struct fcall f;
	uint8_t *msg;
	struct mnt *mnt;
	char *v;
	long l, n;
	unsigned long k;
	int64_t oo;
	char buf[128];

	qlock(&c->umqlock);	/* make sure no one else does this until we've established ourselves */
	if(waserror()){
		qunlock(&c->umqlock);
		nexterror();
	}

	/* defaults */
	if(msize == 0)
		msize = MAXRPC;
	if(msize > c->iounit && c->iounit != 0)
		msize = c->iounit;
	v = version;
	if(v == NULL || v[0] == '\0')
		v = VERSION9P;

	/* validity */
	if(msize < 0)
		error("bad iounit in version call");
	if(strncmp(v, VERSION9P, strlen(VERSION9P)) != 0)
		error("bad 9P version specification");

	mnt = c->mux;

	if(mnt != NULL){
		qunlock(&c->umqlock);
		poperror();

		strncpy(buf, mnt->version, sizeof buf);
		k = strlen(buf);
		if(strncmp(buf, v, k) != 0){
			snprintf(buf, sizeof buf, "incompatible 9P versions %s %s", mnt->version, v);
			error(buf);
		}
		if(returnlen != 0){
			if(returnlen < k)
				error(Eshort);
			memmove(version, buf, k);
		}
		return k;
	}

	f.type = Tversion;
	f.tag = NOTAG;
	f.msize = msize;
	f.version = v;
	msg = kzmalloc(MAXRPC, 0);
	if(msg == NULL)
		panic("version memory");
	if(waserror()){
		kfree(msg);
		nexterror();
	}
	k = convS2M(&f, msg, MAXRPC);
	if(k == 0)
		error("bad fversion conversion on send");

	spin_lock(&c->lock);
	oo = c->offset;
	c->offset += k;
	spin_unlock(&c->lock);

	l = c->dev->write(c, msg, k, oo);

	if(l < k){
		spin_lock(&c->lock);
		c->offset -= k - l;
		spin_unlock(&c->lock);
		error("short write in fversion");
	}

	/* message sent; receive and decode reply */
	n = c->dev->read(c, msg, MAXRPC, c->offset);
	if(n <= 0)
		error("EOF receiving fversion reply");

	spin_lock(&c->lock);
	c->offset += n;
	spin_unlock(&c->lock);

	l = convM2S(msg, n, &f);
	if(l != n)
		error("bad fversion conversion on reply");
	if(f.type != Rversion){
		if(f.type == Rerror)
			error(f.ename);
		error("unexpected reply type in fversion");
	}
	if(f.msize > msize)
		error("server tries to increase msize in fversion");
	if(f.msize<256 || f.msize>1024*1024)
		error("nonsense value of msize in fversion");
	k = strlen(f.version);
	if(strncmp(f.version, v, k) != 0)
		error("bad 9P version returned from server");

	/* now build Mnt associated with this connection */
	spin_lock(&mntalloc.lock);
	mnt = mntalloc.mntfree;
	if(mnt != NULL)
		mntalloc.mntfree = mnt->list;
	else {
		mnt = kzmalloc(sizeof(struct mnt), 0);
		if(mnt == NULL) {
			spin_unlock(&mntalloc.lock);
			panic("mount devices");
		}
	}
	mnt->list = mntalloc.list;
	mntalloc.list = mnt;
	mnt->version = NULL;
	kstrdup(&mnt->version, f.version);
	mnt->id = mntalloc.id++;
	mnt->q = qopen(10*MAXRPC, 0, NULL, NULL);
	mnt->msize = f.msize;
	spin_unlock(&mntalloc.lock);

	if(returnlen != 0){
		if(returnlen < k)
			error(Eshort);
		memmove(version, f.version, k);
	}

	poperror();	/* msg */
	kfree(msg);

	spin_lock(&mnt->lock);
	mnt->queue = 0;
	mnt->rip = 0;

	c->flag |= CMSG;
	c->mux = mnt;
	mnt->c = c;
	spin_unlock(&mnt->lock);

	poperror();	/* c */
	qunlock(&c->umqlock);

	return k;
}

struct chan*
mntauth(struct chan *c, char *spec)
{
	ERRSTACK(2);
	struct mnt *mnt;
	struct mntrpc *r;

	mnt = c->mux;

	if(mnt == NULL){
		mntversion(c, MAXRPC, VERSION9P, 0);
		mnt = c->mux;
		if(mnt == NULL)
			error(Enoversion);
	}

	c = mntchan();
	if(waserror()) {
		/* Close must not be called since it will
		 * call mnt recursively
		 */
		chanfree(c);
		nexterror();
	}

	r = mntralloc(0, mnt->msize);

	if(waserror()) {
		mntfree(r);
		nexterror();
	}

	r->request.type = Tauth;
	r->request.afid = c->fid;
	r->request.uname = "eve"; //curretn->user;
	r->request.aname = spec;
	mountrpc(mnt, r);

	c->qid = r->reply.aqid;
	c->mchan = mnt->c;
	kref_get(&mnt->c->ref, 1);
	c->mqid = c->qid;
	c->mode = ORDWR;

	poperror();	/* r */
	mntfree(r);

	poperror();	/* c */

	return c;

}

static struct chan*
mntattach(char *muxattach)
{
	ERRSTACK(2);
	struct mnt *mnt;
	struct chan *c;
	struct mntrpc *r;
	struct bogus{
		struct chan	*chan;
		struct chan	*authchan;
		char	*spec;
		int	flags;
	}bogus;

	bogus = *((struct bogus *)muxattach);
	c = bogus.chan;

	mnt = c->mux;

	if(mnt == NULL){
		mntversion(c, 0, NULL, 0);
		mnt = c->mux;
		if(mnt == NULL)
			error(Enoversion);
	}

	c = mntchan();
	if(waserror()) {
		/* Close must not be called since it will
		 * call mnt recursively
		 */
		chanfree(c);
		nexterror();
	}

	r = mntralloc(0, mnt->msize);

	if(waserror()) {
		mntfree(r);
		nexterror();
	}

	r->request.type = Tattach;
	r->request.fid = c->fid;
	if(bogus.authchan == NULL)
		r->request.afid = NOFID;
	else
		r->request.afid = bogus.authchan->fid;
	r->request.uname = "eve"; //up->user;
	r->request.aname = bogus.spec;
	mountrpc(mnt, r);

	c->qid = r->reply.qid;
	c->mchan = mnt->c;
	kref_get(&mnt->c->ref, 1);
	c->mqid = c->qid;

	poperror();	/* r */
	mntfree(r);

	poperror();	/* c */

//	if((bogus.flags & MCACHE) && mfcinit != NULL)
//		c->flag |= CCACHE;
	return c;
}

struct chan*
mntchan(void)
{
	struct chan *c;

	c = devattach('M', 0);
	spin_lock(&mntalloc.lock);
	c->devno = mntalloc.id++;
	spin_unlock(&mntalloc.lock);

	if(c->mchan)
		panic("mntchan non-zero %#p", c->mchan);
	return c;
}

static struct walkqid*
mntwalk(struct chan *c, struct chan *nc, char **name, int nname)
{
	ERRSTACK(2);
	int i, alloc;
	struct mnt *mnt;
	struct mntrpc *r;
	struct walkqid *wq;

	if(nc != NULL)
		printd("mntwalk: nc != NULL\n");
	if(nname > MAXWELEM)
		error("devmnt: too many name elements");
	alloc = 0;
	wq = kzmalloc(sizeof(struct walkqid) + (nname - 1) *
		      sizeof(struct qid), 0);
	if(waserror()){
		if(alloc && wq->clone!=NULL)
			cclose(wq->clone);
		kfree(wq);
		return NULL;
	}

	alloc = 0;
	mnt = mntchk(c);
	r = mntralloc(c, mnt->msize);
	if(nc == NULL){
		nc = devclone(c);
		/*
		 * Until the other side accepts this fid,
		 * we can't mntclose it.
		 * nc->dev remains NULL for now.
		 */
		alloc = 1;
	}
	wq->clone = nc;

	if(waserror()) {
		mntfree(r);
		nexterror();
	}
	r->request.type = Twalk;
	r->request.fid = c->fid;
	r->request.newfid = nc->fid;
	r->request.nwname = nname;
	memmove(r->request.wname, name, nname*sizeof( char *));

	mountrpc(mnt, r);

	if(r->reply.nwqid > nname)
		error("too many QIDs returned by walk");
	if(r->reply.nwqid < nname){
		if(alloc)
			cclose(nc);
		wq->clone = NULL;
		if(r->reply.nwqid == 0){
			kfree(wq);
			wq = NULL;
			goto Return;
		}
	}

	/* move new fid onto mnt device and update its qid */
	if(wq->clone != NULL){
		if(wq->clone != c){
			wq->clone->dev = c->dev;
			//if(wq->clone->dev != NULL)	//XDYNX
			//	devtabincr(wq->clone->dev);
			wq->clone->mchan = c->mchan;
			kref_get(&c->mchan->ref, 1);
		}
		if(r->reply.nwqid > 0)
			wq->clone->qid = r->reply.wqid[r->reply.nwqid-1];
	}
	wq->nqid = r->reply.nwqid;
	for(i=0; i<wq->nqid; i++)
		wq->qid[i] = r->reply.wqid[i];

    Return:
	poperror();
	mntfree(r);
	poperror();
	return wq;
}

static long
mntstat(struct chan *c, uint8_t *dp, long n)
{
	ERRSTACK(2);
	struct mnt *mnt;
	struct mntrpc *r;
	unsigned long nstat;

	if(n < BIT16SZ)
		error(Eshortstat);
	mnt = mntchk(c);
	r = mntralloc(c, mnt->msize);
	if(waserror()) {
		mntfree(r);
		nexterror();
	}
	r->request.type = Tstat;
	r->request.fid = c->fid;
	mountrpc(mnt, r);

	if(r->reply.nstat > n){
		nstat = BIT16SZ;
		PBIT16(dp, r->reply.nstat-2);
	}else{
		nstat = r->reply.nstat;
		memmove(dp, r->reply.stat, nstat);
		validstat(dp, nstat);
		mntdirfix(dp, c);
	}
	poperror();
	mntfree(r);

	return nstat;
}

static struct chan*
mntopencreate(int type, struct chan *c, char *name, int omode, int perm)
{
	ERRSTACK(2);
	struct mnt *mnt;
	struct mntrpc *r;

	mnt = mntchk(c);
	r = mntralloc(c, mnt->msize);
	if(waserror()) {
		mntfree(r);
		nexterror();
	}
	r->request.type = type;
	r->request.fid = c->fid;
	r->request.mode = omode;
	if(type == Tcreate){
		r->request.perm = perm;
		r->request.name = name;
	}
	mountrpc(mnt, r);

	c->qid = r->reply.qid;
	c->offset = 0;
	c->mode = openmode(omode);
	c->iounit = r->reply.iounit;
	if(c->iounit == 0 || c->iounit > mnt->msize-IOHDRSZ)
		c->iounit = mnt->msize-IOHDRSZ;
	c->flag |= COPEN;
	poperror();
	mntfree(r);

	/* 
	if(c->flag & CCACHE)
		mfcopen(c);
	*/

	return c;
}

static struct chan*
mntopen(struct chan *c, int omode)
{
	return mntopencreate(Topen, c, NULL, omode, 0);
}

static void
mntcreate(struct chan *c, char *name, int omode, int perm)
{
	mntopencreate(Tcreate, c, name, omode, perm);
}

static void
mntclunk(struct chan *c, int t)
{
	ERRSTACK(2);
	struct mnt *mnt;
	struct mntrpc *r;

	mnt = mntchk(c);
	r = mntralloc(c, mnt->msize);
	if(waserror()){
		mntfree(r);
		nexterror();
	}

	r->request.type = t;
	r->request.fid = c->fid;
	mountrpc(mnt, r);
	mntfree(r);
	poperror();
}

void
muxclose(struct mnt *mnt)
{
	struct mntrpc *q, *r;

	for(q = mnt->queue; q; q = r) {
		r = q->list;
		mntfree(q);
	}
	mnt->id = 0;
	kfree(mnt->version);
	mnt->version = NULL;
	mntpntfree(mnt);
}

void
mntpntfree(struct mnt *mnt)
{
	struct mnt *f, **l;
	struct queue *q;

	spin_lock(&mntalloc.lock);
	l = &mntalloc.list;
	for(f = *l; f; f = f->list) {
		if(f == mnt) {
			*l = mnt->list;
			break;
		}
		l = &f->list;
	}
	mnt->list = mntalloc.mntfree;
	mntalloc.mntfree = mnt;
	q = mnt->q;
	spin_unlock(&mntalloc.lock);

	qfree(q);
}

static void
mntclose(struct chan *c)
{
	mntclunk(c, Tclunk);
}

static void
mntremove(struct chan *c)
{
	mntclunk(c, Tremove);
}

static long
mntwstat(struct chan *c, uint8_t *dp, long n)
{
	ERRSTACK(2);
	struct mnt *mnt;
	struct mntrpc *r;

	mnt = mntchk(c);
	r = mntralloc(c, mnt->msize);
	if(waserror()) {
		mntfree(r);
		nexterror();
	}
	r->request.type = Twstat;
	r->request.fid = c->fid;
	r->request.nstat = n;
	r->request.stat = dp;
	mountrpc(mnt, r);
	poperror();
	mntfree(r);
	return n;
}

static long
mntread(struct chan *c, void *buf, long n, int64_t off)
{
	uint8_t *p, *e;
	int nc, cache, isdir;
	unsigned long dirlen;

	isdir = 0;
	cache = c->flag & CCACHE;
	if(c->qid.type & QTDIR) {
		cache = 0;
		isdir = 1;
	}

	p = buf;
	if(cache) {
		nc = 0; // mfcread(c, buf, n, off);
		if(nc > 0) {
			n -= nc;
			if(n == 0)
				return nc;
			p += nc;
			off += nc;
		}
		n = mntrdwr(Tread, c, p, n, off);
		//mfcupdate(c, p, n, off);
		return n + nc;
	}

	n = mntrdwr(Tread, c, buf, n, off);
	if(isdir) {
		for(e = &p[n]; p+BIT16SZ < e; p += dirlen){
			dirlen = BIT16SZ+GBIT16(p);
			if(p+dirlen > e)
				break;
			validstat(p, dirlen);
			mntdirfix(p, c);
		}
		if(p != e)
			error(Esbadstat);
	}
	return n;
}

static long
mntwrite(struct chan *c, void *buf, long n, int64_t off)
{
	return mntrdwr(Twrite, c, buf, n, off);
}

long
mntrdwr(int type, struct chan *c, void *buf, long n, int64_t off)
{
	ERRSTACK(1);
	struct mnt *mnt;
 	struct mntrpc *r;
	char *uba;
	int cache;
	uint32_t cnt, nr, nreq;
	int got;

	mnt = mntchk(c);
	uba = buf;
	cnt = 0;
	cache = c->flag & CCACHE;
	if(c->qid.type & QTDIR)
		cache = 0;
	for(;;) {
		r = mntralloc(c, mnt->msize);
		if(waserror()) {
			mntfree(r);
			nexterror();
		}
		r->request.type = type;
		r->request.fid = c->fid;
		r->request.offset = off;
		r->request.data = uba;
		nr = n;
		if(nr > mnt->msize-IOHDRSZ)
			nr = mnt->msize-IOHDRSZ;
		r->request.count = nr;
		mountrpc(mnt, r);
		nreq = r->request.count;
		nr = r->reply.count;
		if(nr > nreq)
			nr = nreq;

		got = nr;
		if(type == Tread)
			r->b = bl2mem(( uint8_t *)uba, r->b, &got);
		//else if(cache)
		//mfcwrite(c, ( uint8_t *)uba, nr, off);

		poperror();
		mntfree(r);
		off += nr;
		uba += nr;
		cnt += nr;
		n -= nr;
		if(nr != nreq || n == 0 ) // signal pending|| up->nnote)
			break;
	}
	return cnt;
}

void
mountrpc(struct mnt *mnt, struct mntrpc *r)
{
	char *sn, *cn;
	int t;

	r->reply.tag = 0;
	r->reply.type = Tmax;	/* can't ever be a valid message type */

	mountio(mnt, r);

	t = r->reply.type;
	switch(t) {
	case Rerror:
		error(r->reply.ename);
	case Rflush:
		error(Eintr);
	default:
		if(t == r->request.type+1)
			break;
		sn = "?";
		if(mnt->c->path != NULL)
			sn = mnt->c->path->s;
		cn = "?";
		if(r->c != NULL && r->c->path != NULL)
			cn = r->c->path->s;
		printd("mnt: proc %s %d: mismatch from %s %s rep %#p tag %d fid %d T%d R%d rp %d\n",
			up->text, up->pid, sn, cn,
			r, r->request.tag, r->request.fid, r->request.type,
			r->reply.type, r->reply.tag);
		error(Emountrpc);
	}
}

void
mountio(struct mnt *mnt, struct mntrpc *r)
{
	ERRSTACK(1);
	int n;

	while(waserror()) {
		if(mnt->rip == current)
			mntgate(mnt);
		/* TODO: figure out if we had a signal.
		 * if so, get out of here. For now,
		 * assume we did.
		 */
		if(/*strcmp(up->errstr, Eintr) != 0*/1){
			mntflushfree(mnt, r);
			nexterror();
		}
		r = mntflushalloc(r, mnt->msize);
	}

	spin_lock(&mnt->lock);
	r->m = mnt;
	r->list = mnt->queue;
	mnt->queue = r;
	spin_unlock(&mnt->lock);

	/* Transmit a file system rpc */
	if(mnt->msize == 0)
		panic("msize");
	n = convS2M(&r->request, r->rpc, mnt->msize);
	if(n < 0)
		panic("bad message type in mountio");
	if(mnt->c->dev->write(mnt->c, r->rpc, n, 0) != n)
		error(Emountrpc);
	r->stime = read_tsc_serialized();//fastticks(NULL);
	r->reqlen = n;

	/* Gate readers onto the mount point one at a time */
	for(;;) {
		spin_lock(&mnt->lock);
		if(mnt->rip == 0)
			break;
		spin_unlock(&mnt->lock);
		rendez_sleep(&r->r, rpcattn, r);
		if(r->done){
			poperror();
			mntflushfree(mnt, r);
			return;
		}
	}
	mnt->rip = current;
	spin_unlock(&mnt->lock);
	while(r->done == 0) {
		if(mntrpcread(mnt, r) < 0)
			error(Emountrpc);
		mountmux(mnt, r);
	}
	mntgate(mnt);
	poperror();
	mntflushfree(mnt, r);
}

/* call the device read and fill the queue with blocks from it. */
static int
doread(struct mnt *mnt, int len)
{
	struct block *b;

	while(qlen(mnt->q) < len){
		b = mnt->c->dev->bread(mnt->c, mnt->msize, 0);
		if(b == NULL)
			return -1;
		if(blocklen(b) == 0){
			freeblist(b);
			return -1;
		}
		qaddlist(mnt->q, b);
	}
	return 0;
}

int
mntrpcread(struct mnt *mnt, struct mntrpc *r)
{
	int i, t, len, hlen;
	struct block *b, **l, *nb;
	uint8_t *header;

	r->reply.type = 0;
	r->reply.tag = 0;

	/* read at least length, type, and tag and pullup to a single block */
	if(doread(mnt, BIT32SZ+BIT8SZ+BIT16SZ) < 0)
		return -1;
	/* size for the worst case. */
	header = kzmalloc(BIT32SZ + BIT8SZ + BIT16SZ + BIT32SZ, KMALLOC_WAIT);
	/* but the additional 32 bits is only on read.
	 * Were it me, I would have made all headers same and had an
	 * unused field and avoided this extra work. :-)
	 */
	if (qread(mnt->q, header, BIT32SZ+BIT8SZ+BIT16SZ) < BIT32SZ+BIT8SZ+BIT16SZ)
		error("mntrpcread: can't happen 1");

	/* read in the rest of the message, avoid ridiculous (for now) message sizes */
	len = GBIT32(header);
	if(len > mnt->msize){
		qdiscard(mnt->q, qlen(mnt->q));
		return -1;
	}
	if(doread(mnt, len-BIT32SZ+BIT8SZ+BIT16SZ) < 0)
		return -1;

	/* get the header (i.e. everything except data) */
	t = header[BIT32SZ];
	switch(t){
	case Rread:
		hlen = BIT32SZ+BIT8SZ+BIT16SZ+BIT32SZ;
		break;
	default:
		hlen = len;
		break;
	}
	/* read in the rest of the read request. */
	if (qread(mnt->q, &header[BIT32SZ+BIT8SZ+BIT16SZ],
		       BIT32SZ) < BIT32SZ)
		error("mntrpcread: can't happen 2");

	if(convM2S(header, len, &r->reply) <= 0){
		/* bad message, dump it */
		printd("mntrpcread: convM2S failed\n");
		qdiscard(mnt->q, len);
		return -1;
	}

	/* hang the data off of the fcall struct */
	l = &r->b;
	b = qbread(mnt->q,len-hlen);
	*l = b;

	return 0;
}

void
mntgate(struct mnt *mnt)
{
	struct mntrpc *q;

	spin_lock(&mnt->lock);
	mnt->rip = 0;
	for(q = mnt->queue; q; q = q->list) {
		if(q->done == 0)
			/* TODO: wakedup */
			if(/*wakeup(&q->r)*/1)
				break;
	}
	spin_unlock(&mnt->lock);
}

void
mountmux(struct mnt *mnt, struct mntrpc *r)
{
	struct mntrpc **l, *q;

	spin_lock(&mnt->lock);
	l = &mnt->queue;
	for(q = *l; q; q = q->list) {
		/* look for a reply to a message */
		if(q->request.tag == r->reply.tag) {
			*l = q->list;
			if(q != r) {
				/*
				 * Completed someone else.
				 * Trade pointers to receive buffer.
				 */
				q->reply = r->reply;
				q->b = r->b;
				r->b = NULL;
			}
			q->done = 1;
			if(mntstats != NULL)
				(*mntstats)(q->request.type,
					mnt->c, q->stime,
					q->reqlen + r->replen);
			if(q != r)
				rendez_wakeup(&q->r);
			spin_unlock(&mnt->lock);
			return;
		}
		l = &q->list;
	}
	spin_unlock(&mnt->lock);
	printd("unexpected reply tag %u; type %d\n", r->reply.tag, r->reply.type);
}

/*
 * Create a new flush request and chain the previous
 * requests from it
 */
struct mntrpc*
mntflushalloc(struct mntrpc *r, uint32_t iounit)
{
	struct mntrpc *fr;

	fr = mntralloc(0, iounit);

	fr->request.type = Tflush;
	if(r->request.type == Tflush)
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
void
mntflushfree(struct mnt *mnt, struct mntrpc *r)
{
	struct mntrpc *fr;

	while(r){
		fr = r->flushed;
		if(!r->done){
			r->reply.type = Rflush;
			mntqrm(mnt, r);
		}
		if(fr)
			mntfree(r);
		r = fr;
	}
}

int
alloctag(void)
{
	int i, j;
	uint32_t v;

	for(i = 0; i < NMASK; i++){
		v = mntalloc.tagmask[i];
		if(v == ~0UL)
			continue;
		for(j = 0; j < 1<<TAGSHIFT; j++)
			if((v & (1<<j)) == 0){
				mntalloc.tagmask[i] |= 1<<j;
				return (i<<TAGSHIFT) + j;
			}
	}
	panic("no friggin tags left");
	return NOTAG;
}

void
freetag(int t)
{
	mntalloc.tagmask[t>>TAGSHIFT] &= ~(1<<(t&TAGMASK));
}

struct mntrpc*
mntralloc(struct chan *c, uint32_t msize)
{
	struct mntrpc *new;

	spin_lock(&mntalloc.lock);
	new = mntalloc.rpcfree;
	if(new == NULL){
		new = kzmalloc(sizeof(struct mntrpc), 0);
		if(new == NULL) {
			spin_unlock(&mntalloc.lock);
			panic("mount rpc header");
		}
		rendez_init(&new->r);
		/*
		 * The header is split from the data buffer as
		 * mountmux may swap the buffer with another header.
		 */
		new->rpc = kzmalloc(msize, 0);
		if(new->rpc == NULL){
			kfree(new);
			spin_unlock(&mntalloc.lock);
			panic("mount rpc buffer");
		}
		new->rpclen = msize;
		new->request.tag = alloctag();
	}
	else {
		mntalloc.rpcfree = new->list;
		mntalloc.nrpcfree--;
		if(new->rpclen < msize){
			kfree(new->rpc);
			new->rpc = kzmalloc(msize, 0);
			if(new->rpc == NULL){
				kfree(new);
				mntalloc.nrpcused--;
				spin_unlock(&mntalloc.lock);
				panic("mount rpc buffer");
			}
			new->rpclen = msize;
		}
	}
	mntalloc.nrpcused++;
	spin_unlock(&mntalloc.lock);
	new->c = c;
	new->done = 0;
	new->flushed = NULL;
	new->b = NULL;
	return new;
}

void
mntfree(struct mntrpc *r)
{
	if(r->b != NULL)
		freeblist(r->b);
	spin_lock(&mntalloc.lock);
	if(mntalloc.nrpcfree >= 10){
		kfree(r->rpc);
		freetag(r->request.tag);
		kfree(r);
	}
	else{
		r->list = mntalloc.rpcfree;
		mntalloc.rpcfree = r;
		mntalloc.nrpcfree++;
	}
	mntalloc.nrpcused--;
	spin_unlock(&mntalloc.lock);
}

void
mntqrm(struct mnt *mnt, struct mntrpc *r)
{
	struct mntrpc **l, *f;

	spin_lock(&mnt->lock);
	r->done = 1;

	l = &mnt->queue;
	for(f = *l; f; f = f->list) {
		if(f == r) {
			*l = r->list;
			break;
		}
		l = &f->list;
	}
	spin_unlock(&mnt->lock);
}

struct mnt*
mntchk(struct chan *c)
{
	struct mnt *mnt;

	/* This routine is mostly vestiges of prior lives; now it's just sanity checking */

	if(c->mchan == NULL)
		panic("mntchk 1: NULL mchan c %s", chanpath(c));

	mnt = c->mchan->mux;

	if(mnt == NULL)
		printd("mntchk 2: NULL mux c %s c->mchan %s \n", chanpath(c), chanpath(c->mchan));

	/*
	 * Was it closed and reused (was error(Eshutdown); now, it cannot happen)
	 */
	if(mnt->id == 0 || mnt->id >= c->devno)
		panic("mntchk 3: can't happen");

	return mnt;
}

/*
 * Rewrite channel type and dev for in-flight data to
 * reflect local values.  These entries are known to be
 * the first two in the Dir encoding after the count.
 */
void
mntdirfix(uint8_t *dirbuf, struct chan *c)
{
	unsigned int r;

	r = c->dev->dc;
	dirbuf += BIT16SZ;	/* skip count */
	PBIT16(dirbuf, r);
	dirbuf += BIT16SZ;
	PBIT32(dirbuf, c->devno);
}

int
rpcattn(void *v)
{
	struct mntrpc *r;

	r = v;
	return r->done || r->m->rip == 0;
}

struct dev mntdevtab = {
	'M',
	"mnt",

	mntreset,
	devinit,
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
	devconfig,
	devchaninfo,
};
