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

struct pipe
{
	qlock_t qlock;
	struct pipe	*next;
	struct kref	ref;
	uint32_t	path;
	struct queue	*q[2];
	int	qref[2];
};

struct
{
	spinlock_t lock;
	uint32_t	path;
} pipealloc;

enum
{
	Qdir,
	Qdata0,
	Qdata1,
};

struct dirtab pipedir[] =
{
	{".",		{Qdir,0,QTDIR},	0,		DMDIR|0500},
	{"data",		{Qdata0},	0,		0600},
	{"data1",	{Qdata1},	0,		0600},
};
#define NPIPEDIR 3

#define PIPETYPE(x)	(((unsigned)x)&0x1f)
#define PIPEID(x)	((((unsigned)x))>>5)
#define PIPEQID(i, t)	((((unsigned)i)<<5)|(t))


enum
{
	/* Plan 9 default for nmach > 1 */
	Pipeqsize = 256*1024
};

static void pipe_release(struct kref *kref)
{
	printd("pipe release\n");
}

static void
pipeinit(void)
{
}

/*
 *  create a pipe, no streams are created until an open
 */
static struct chan*
pipeattach(char *spec, struct errbuf *perrbuf)
{
	struct pipe *p;
	struct chan *c;

	c = devattach('|', spec, perrbuf);
	p = kzmalloc(sizeof(struct pipe), 0);
	if(p == 0)
		panic("memory");
	kref_init(&p->ref, pipe_release, 1);

	p->q[0] = qopen(Pipeqsize, 0, 0, 0);
	if(p->q[0] == 0){
		kfree(p);
		panic("memory");
	}
	p->q[1] = qopen(Pipeqsize, 0, 0, 0);
	if(p->q[1] == 0){
		kfree(p->q[0]);
		kfree(p);
		panic("memory");
	}

	spin_lock(&pipealloc.lock);
	p->path = ++pipealloc.path;
	spin_unlock(&pipealloc.lock);

	mkqid(&c->qid, PIPEQID(2*p->path, Qdir), 0, QTDIR, perrbuf);
	c->aux = p;
	c->devno = 0;
	return c;
}

static int
pipegen(struct chan *c, char*unused, struct dirtab *tab, int ntab, int i, struct dir *dp, struct errbuf *perrbuf)
{
	struct qid q;
	int len;
	struct pipe *p;

	if(i == DEVDOTDOT){
		devdir(c, c->qid, "#|", 0, eve, DMDIR|0555, dp);
		return 1;
	}
	i++;	/* skip . */
	if(tab==0 || i>=ntab)
		return -1;

	tab += i;
	p = c->aux;
	switch((uint32_t)tab->qid.path){
	case Qdata0:
		len = qlen(p->q[0]);
		break;
	case Qdata1:
		len = qlen(p->q[1]);
		break;
	default:
		len = tab->length;
		break;
	}
	mkqid(&q, PIPEQID(PIPEID(c->qid.path), tab->qid.path), 0, QTFILE, perrbuf);
	devdir(c, q, tab->name, len, eve, tab->perm, dp);
	return 1;
}


static struct walkqid*
pipewalk(struct chan *c, struct chan *nc, char **name, int nname, struct errbuf *perrbuf)
{
	struct walkqid *wq;
	struct pipe *p;

	wq = devwalk(c, nc, name, nname, pipedir, NPIPEDIR, pipegen, perrbuf);
	if(wq != NULL && wq->clone != NULL && wq->clone != c){
		p = c->aux;
		qlock(&p->qlock);
		kref_get(&p->ref, 1);
		if(c->flag & COPEN){
			printd("channel open in pipewalk\n");
			switch(PIPETYPE(c->qid.path)){
			case Qdata0:
				p->qref[0]++;
				break;
			case Qdata1:
				p->qref[1]++;
				break;
			}
		}
		qunlock(&p->qlock);
	}
	return wq;
}

static long
pipestat(struct chan *c, uint8_t *db, long n, struct errbuf *perrbuf)
{
	struct pipe *p;
	struct dir dir;

	p = c->aux;

	switch(PIPETYPE(c->qid.path)){
	case Qdir:
		devdir(c, c->qid, ".", 0, eve, DMDIR|0555, &dir);
		break;
	case Qdata0:
		devdir(c, c->qid, "data", qlen(p->q[0]), eve, 0600, &dir);
		break;
	case Qdata1:
		devdir(c, c->qid, "data1", qlen(p->q[1]), eve, 0600, &dir);
		break;
	default:
		panic("pipestat");
	}
	n = convD2M(&dir, db, n);
	if(n < sizeof(uint16_t))
		error(Eshortstat);
	return n;
}

/*
 *  if the stream doesn't exist, create it
 */
static struct chan*
pipeopen(struct chan *c, int omode, struct errbuf *perrbuf)
{
	struct pipe *p;

	if(c->qid.type & QTDIR){
		if(omode != OREAD)
			error(Ebadarg);
		c->mode = omode;
		c->flag |= COPEN;
		c->offset = 0;
		return c;
	}

	p = c->aux;
	qlock(&p->qlock);
	switch(PIPETYPE(c->qid.path)){
	case Qdata0:
		p->qref[0]++;
		break;
	case Qdata1:
		p->qref[1]++;
		break;
	}
	qunlock(&p->qlock);

	c->mode = openmode(omode, perrbuf);
	c->flag |= COPEN;
	c->offset = 0;
	c->iounit = qiomaxatomic;
	return c;
}

static void
pipeclose(struct chan *c, struct errbuf *perrbuf)
{
	struct pipe *p;

	p = c->aux;
	qlock(&p->qlock);

	if(c->flag & COPEN){
		/*
		 *  closing either side hangs up the stream
		 */
		switch(PIPETYPE(c->qid.path)){
		case Qdata0:
			p->qref[0]--;
			if(p->qref[0] == 0){
				qhangup(p->q[1], 0);
				qclose(p->q[0]);
			}
			break;
		case Qdata1:
			p->qref[1]--;
			if(p->qref[1] == 0){
				qhangup(p->q[0], 0);
				qclose(p->q[1]);
			}
			break;
		}
	}


	/*
	 *  if both sides are closed, they are reusable
	 */
	if(p->qref[0] == 0 && p->qref[1] == 0){
		qreopen(p->q[0]);
		qreopen(p->q[1]);
	}

	/*
	 *  free the structure on last close
	 */
	kref_put(&p->ref);
	if(kref_refcnt(&p->ref) == 0){
		qunlock(&p->qlock);
		kfree(p->q[0]);
		kfree(p->q[1]);
		kfree(p);
	} else
		qunlock(&p->qlock);
}

static long
piperead(struct chan *c, void *va, long n, int64_t unused, struct errbuf *perrbuf)
{
	struct pipe *p;

	p = c->aux;

	switch(PIPETYPE(c->qid.path)){
	case Qdir:
		return devdirread(c, va, n, pipedir, NPIPEDIR, pipegen, perrbuf);
	case Qdata0:
		return qread(p->q[0], va, n, perrbuf);
	case Qdata1:
		return qread(p->q[1], va, n, perrbuf);
	default:
		panic("piperead");
	}
	return -1;	/* not reached */
}

static struct block*
pipebread(struct chan *c, long n, int64_t offset, struct errbuf *perrbuf)
{
	struct pipe *p;

	p = c->aux;

	switch(PIPETYPE(c->qid.path)){
	case Qdata0:
		return qbread(p->q[0], n, perrbuf);
	case Qdata1:
		return qbread(p->q[1], n, perrbuf);
	}

	return devbread(c, n, offset, perrbuf);
}

/*
 *  a write to a closed pipe causes a note to be sent to
 *  the process.
 */
static long
pipewrite(struct chan *c, void *va, long n, int64_t unused, struct errbuf *perrbuf)
{
	ERRSTACK(2);
	struct pipe *p;

	if(waserror()) {
		/* how do we deliver this one
		if((c->flag & CMSG) == 0)
			postnote(up, 1, "sys: write on closed pipe", NUser);
		*/
		nexterror();
	}

	p = c->aux;

	switch(PIPETYPE(c->qid.path)){
	case Qdata0:
		n = qwrite(p->q[1], va, n, perrbuf);
		break;

	case Qdata1:
		n = qwrite(p->q[0], va, n, perrbuf);
		break;

	default:
		panic("pipewrite");
	}

	return n;
}

static long
pipebwrite(struct chan *c, struct block *bp, int64_t unused, struct errbuf *perrbuf)
{
	ERRSTACK(2);
	long n;
	struct pipe *p;

	if(waserror()) {
		/* avoid notes when pipe is a mounted queue 
		   how do we do this
		if((c->flag & CMSG) == 0)
			postnote(up, 1, "sys: write on closed pipe", NUser);
		*/
		nexterror();
	}

	p = c->aux;
	switch(PIPETYPE(c->qid.path)){
	case Qdata0:
		n = qbwrite(p->q[1], bp, perrbuf);
		break;

	case Qdata1:
		n = qbwrite(p->q[0], bp, perrbuf);
		break;

	default:
		n = 0;
		panic("pipebwrite");
	}

	return n;
}

struct dev pipedevtab = {
	'|',
	"pipe",

	devreset,
	pipeinit,
	devshutdown,
	pipeattach,
	pipewalk,
	pipestat,
	pipeopen,
	devcreate,
	pipeclose,
	piperead,
	pipebread,
	pipewrite,
	pipebwrite,
	devremove,
	devwstat,
};
