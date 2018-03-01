/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */

//#define DEBUG
/* proc on plan 9 has lots of capabilities, some of which we might
 * want for akaros:
 * debug control
 * event tracing
 * process control (no need for signal system call, etc.)
 * textual status
 * rather than excise code that won't work, I'm bracketing it with
 * #if 0 until we know we don't want it
 */
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
#include <umem.h>
#include <arch/vmm/vmm.h>
#include <ros/vmm.h>

struct dev procdevtab;

static char *devname(void)
{
	return procdevtab.name;
}

enum {
	Qdir,
	Qtrace,
	Qtracepids,
	Qself,
	Qns,
	Qargs,
	Qctl,
	Qfd,
	Qfpregs,
	Qkregs,
	Qmaps,
	Qmem,
	Qnote,
	Qnoteid,
	Qnotepg,
	Qproc,
	Qregs,
	Quser,
	Qsegment,
	Qstatus,
	Qstrace,
	Qstrace_traceset,
	Qvmstatus,
	Qtext,
	Qwait,
	Qprofile,
	Qsyscall,
	Qcore,
};

enum {
	CMclose,
	CMclosefiles,
	CMhang,
	CMstraceme,
	CMstraceall,
	CMstrace_drop,
};

enum {
	Nevents = 0x4000,
	Emask = Nevents - 1,
	Ntracedpids = 1024,
	STATSIZE = 8 + 1 + 10 + 1 + 6 + 2,
};

/*
 * Status, fd, and ns are left fully readable (0444) because of their use in debugging,
 * particularly on shared servers.
 * Arguably, ns and fd shouldn't be readable; if you'd prefer, change them to 0000
 */
struct dirtab procdir[] = {
	{"args", {Qargs}, 0, 0660},
	{"ctl", {Qctl}, 0, 0660},
	{"fd", {Qfd}, 0, 0444},
	{"fpregs", {Qfpregs}, 0, 0000},
	//  {"kregs",   {Qkregs},   sizeof(Ureg),       0600},
	{"maps", {Qmaps}, 0, 0000},
	{"mem", {Qmem}, 0, 0000},
	{"note", {Qnote}, 0, 0000},
	{"noteid", {Qnoteid}, 0, 0664},
	{"notepg", {Qnotepg}, 0, 0000},
	{"ns", {Qns}, 0, 0444},
	{"proc", {Qproc}, 0, 0400},
	//  {"regs",        {Qregs},    sizeof(Ureg),       0000},
	{"user", {Quser}, 0, 0444},
	{"segment", {Qsegment}, 0, 0444},
	{"status", {Qstatus}, STATSIZE, 0444},
	{"strace", {Qstrace}, 0, 0444},
	{"strace_traceset", {Qstrace_traceset}, 0, 0666},
	{"vmstatus", {Qvmstatus}, 0, 0444},
	{"text", {Qtext}, 0, 0000},
	{"wait", {Qwait}, 0, 0400},
	{"profile", {Qprofile}, 0, 0400},
	{"syscall", {Qsyscall}, 0, 0400},
	{"core", {Qcore}, 0, 0444},
};

static
struct cmdtab proccmd[] = {
	{CMclose, "close", 2},
	{CMclosefiles, "closefiles", 0},
	{CMhang, "hang", 0},
	{CMstraceme, "straceme", 0},
	{CMstraceall, "straceall", 0},
	{CMstrace_drop, "strace_drop", 2},
};

/*
 * struct qids are, in path:
 *	 5 bits of file type (qids above) (old comment said 4 here)
 *	23 bits of process slot number + 1 (pid + 1 is stored)
 *	     in vers,
 *	32 bits of pid, for consistency checking
 * If notepg, c->pgrpid.path is pgrp slot, .vers is noteid.
 */
#define	QSHIFT	5	/* location in qid of proc slot # */
#define	SLOTBITS 23	/* number of bits in the slot */
#define	QIDMASK	((1<<QSHIFT)-1)
#define	SLOTMASK	(((1<<SLOTBITS)-1) << QSHIFT)

#define QID(q)		((((uint32_t)(q).path)&QIDMASK)>>0)
#define SLOT(q)		(((((uint32_t)(q).path)&SLOTMASK)>>QSHIFT)-1)
#define PID(q)		((q).vers)
#define NOTEID(q)	((q).vers)

static void procctlreq(struct proc *, char *, int);
static int procctlmemio(struct proc *, uintptr_t, int, void *, int);
//static struct chan*   proctext(struct chan*, struct proc*);
//static Segment* txt2data(struct proc*, Segment*);
//static int    procstopped(void*);
static void mntscan(struct mntwalk *, struct proc *);

//static Traceevent *tevents;
static char *tpids, *tpidsc, *tpidse;
static spinlock_t tlock;
static int topens;
static int tproduced, tconsumed;
//static void notrace(struct proc*, int, int64_t);

//void (*proctrace)(struct proc*, int, int64_t) = notrace;

#if 0
static void profclock(Ureg * ur, Timer *)
{
	Tos *tos;

	if (up == NULL || current->state != Running)
		return;

	/* user profiling clock */
	if (userureg(ur)) {
		tos = (Tos *) (USTKTOP - sizeof(Tos));
		tos->clock += TK2MS(1);
		segclock(userpc(ur));
	}
}
#endif
static int
procgen(struct chan *c, char *name, struct dirtab *tab, int unused, int s,
		struct dir *dp)
{
	struct qid qid;
	struct proc *p;
	char *ename;

	int pid;
	uint32_t path, perm, len;
	if (s == DEVDOTDOT) {
		mkqid(&qid, Qdir, 0, QTDIR);
		devdir(c, qid, devname(), 0, eve.name, 0555, dp);
		return 1;
	}

	if (c->qid.path == Qdir) {
		if (s == 0) {
			strlcpy(get_cur_genbuf(), "trace", GENBUF_SZ);
			mkqid(&qid, Qtrace, -1, QTFILE);
			devdir(c, qid, get_cur_genbuf(), 0, eve.name, 0444, dp);
			return 1;
		}
		if (s == 1) {
			strlcpy(get_cur_genbuf(), "tracepids", GENBUF_SZ);
			mkqid(&qid, Qtracepids, -1, QTFILE);
			devdir(c, qid, get_cur_genbuf(), 0, eve.name, 0444, dp);
			return 1;
		}
		if (s == 2) {
			p = current;
			strlcpy(get_cur_genbuf(), "self", GENBUF_SZ);
			mkqid(&qid, (p->pid + 1) << QSHIFT, p->pid, QTDIR);
			devdir(c, qid, get_cur_genbuf(), 0, p->user.name, DMDIR | 0555, dp);
			return 1;
		}
		s -= 3;
		if (name != NULL) {
			/* ignore s and use name to find pid */
			pid = strtol(name, &ename, 10);
			if (pid <= 0 || ename[0] != '\0')
				return -1;
			p = pid2proc(pid);
			if (!p)
				return -1;
			/* Need to update s, so that it's the correct 'index' for our proc
			 * (aka, the pid).  We use s later when making the qid. */
			s = pid;
		} else {
			/* This is a shitty iterator, and the list isn't guaranteed to give
			 * you the same ordering twice in a row. (procs come and go). */
			p = pid_nth(s);
			if (!p)
				return -1;
			pid = p->pid;
		}

		snprintf(get_cur_genbuf(), GENBUF_SZ, "%u", pid);
		/*
		 * String comparison is done in devwalk so
		 * name must match its formatted pid.
		 */
		if (name != NULL && strcmp(name, get_cur_genbuf()) != 0) {
			printk("pid-name mismatch, name: %s, pid %d\n", name, pid);
			proc_decref(p);
			return -1;
		}
		mkqid(&qid, (s + 1) << QSHIFT, pid, QTDIR);
		devdir(c, qid, get_cur_genbuf(), 0, p->user.name, DMDIR | 0555, dp);
		proc_decref(p);
		return 1;
	}
	if (c->qid.path == Qtrace) {
		strlcpy(get_cur_genbuf(), "trace", GENBUF_SZ);
		mkqid(&qid, Qtrace, -1, QTFILE);
		devdir(c, qid, get_cur_genbuf(), 0, eve.name, 0444, dp);
		return 1;
	}
	if (c->qid.path == Qtracepids) {
		strlcpy(get_cur_genbuf(), "tracepids", GENBUF_SZ);
		mkqid(&qid, Qtracepids, -1, QTFILE);
		devdir(c, qid, get_cur_genbuf(), 0, eve.name, 0444, dp);
		return 1;
	}
	if (s >= ARRAY_SIZE(procdir))
		return -1;
	if (tab)
		panic("procgen");

	tab = &procdir[s];
	/* path is everything other than the QID part.  Not sure from the orig code
	 * if they wanted just the pid part (SLOTMASK) or everything above QID */
	path = c->qid.path & ~QIDMASK;	/* slot component */
	if ((p = pid2proc(SLOT(c->qid))) == NULL)
		return -1;
	perm = 0444 | tab->perm;
#if 0
	if (perm == 0)
		perm = p->procmode;
	else	/* just copy read bits */
		perm |= p->procmode & 0444;
#endif

	len = tab->length;
#if 0
	switch (QID(c->qid)) {
		case Qwait:
			len = p->nwait;	/* incorrect size, but >0 means there's something to read */
			break;
		case Qprofile:
			q = p->seg[TSEG];
			if (q && q->profile) {
				len = (q->top - q->base) >> LRESPROF;
				len *= sizeof(*q->profile);
			}
			break;
	}
#endif

	mkqid(&qid, path | tab->qid.path, c->qid.vers, QTFILE);
	devdir(c, qid, tab->name, len, p->user.name, perm, dp);
	proc_decref(p);
	return 1;
}

#if 0
static void notrace(struct proc *, Tevent, int64_t)
{
}

static spinlock_t tlck = SPINLOCK_INITIALIZER_IRQSAVE;

static void _proctrace(struct proc *p, Tevent etype, int64_t ts)
{
	Traceevent *te;
	int tp;

	ilock(&tlck);
	if (p->trace == 0 || topens == 0 || tproduced - tconsumed >= Nevents) {
		iunlock(&tlck);
		return;
	}
	tp = tproduced++;
	iunlock(&tlck);

	te = &tevents[tp & Emask];
	te->pid = p->pid;
	te->etype = etype;
	if (ts == 0)
		te->time = todget(NULL);
	else
		te->time = ts;
	te->core = m->machno;
}

void proctracepid(struct proc *p)
{
	if (p->trace == 1 && proctrace != notrace) {
		p->trace = 2;
		ilock(&tlck);
		tpidsc = seprint(tpidsc, tpidse, "%d %s\n", p->pid, p->text);
		iunlock(&tlck);
	}
}

#endif
static void procinit(void)
{
#if 0
	if (conf.nproc >= (SLOTMASK >> QSHIFT) - 1)
		printd("warning: too many procs for devproc\n");
	addclock0link((void (*)(void))profclock, 113);	/* Relative prime to HZ */
#endif
}

static struct chan *procattach(char *spec)
{
	return devattach(devname(), spec);
}

static struct walkqid *procwalk(struct chan *c, struct chan *nc, char **name,
								unsigned int nname)
{
	return devwalk(c, nc, name, nname, 0, 0, procgen);
}

static size_t procstat(struct chan *c, uint8_t *db, size_t n)
{
	return devstat(c, db, n, 0, 0, procgen);
}

/*
 *  none can't read or write state on other
 *  processes.  This is to contain access of
 *  servers running as none should they be
 *  subverted by, for example, a stack attack.
 */
static void nonone(struct proc *p)
{
	return;
#if 0
	if (p == up)
		return;
	if (strcmp(current->user.name, "none") != 0)
		return;
	if (iseve())
		return;
	error(EPERM, ERROR_FIXME);
#endif
}

struct bm_helper {
	void						*buf;
	size_t						buflen;
	size_t						sofar;
};

static void get_needed_sz_cb(struct vm_region *vmr, void *arg)
{
	struct bm_helper *bmh = (struct bm_helper*)arg;

	/* ballpark estimate of a line */
	bmh->buflen += 150;
}

static void build_maps_cb(struct vm_region *vmr, void *arg)
{
	struct bm_helper *bmh = (struct bm_helper*)arg;
	size_t old_sofar;
	char path_buf[MAX_FILENAME_SZ];
	char *path;
	unsigned long inode_nr;

	if (vmr_has_file(vmr)) {
		path = foc_abs_path(vmr->__vm_foc, path_buf, sizeof(path_buf));
		inode_nr = 0; /* TODO: do we care about this? */
	} else {
		strlcpy(path_buf, "[heap]", sizeof(path_buf));
		path = path_buf;
		inode_nr = 0;
	}

	old_sofar = bmh->sofar;
	bmh->sofar += snprintf(bmh->buf + bmh->sofar, bmh->buflen - bmh->sofar,
	                       "%08lx-%08lx %c%c%c%c %08x %02d:%02d %d ",
	                       vmr->vm_base, vmr->vm_end,
	                       vmr->vm_prot & PROT_READ    ? 'r' : '-',
	                       vmr->vm_prot & PROT_WRITE   ? 'w' : '-',
	                       vmr->vm_prot & PROT_EXEC    ? 'x' : '-',
	                       vmr->vm_flags & MAP_PRIVATE ? 'p' : 's',
	                       vmr_has_file(vmr) ? vmr->vm_foff : 0,
	                       vmr_has_file(vmr) ? 1 : 0,	/* VFS == 1 for major */
	                       0,
	                       inode_nr);
	/* Align the filename to the 74th char, like Linux (73 chars so far) */
	bmh->sofar += snprintf(bmh->buf + bmh->sofar, bmh->buflen - bmh->sofar,
	                       "%*s", 73 - (bmh->sofar - old_sofar), "");
	bmh->sofar += snprintf(bmh->buf + bmh->sofar, bmh->buflen - bmh->sofar,
	                       "%s\n", path);
}

static struct sized_alloc *build_maps(struct proc *p)
{
	struct bm_helper bmh[1];
	struct sized_alloc *sza;

	/* Try to figure out the size needed: start with extra space, then add a bit
	 * for each VMR */
	bmh->buflen = 150;
	enumerate_vmrs(p, get_needed_sz_cb, bmh);
	sza = sized_kzmalloc(bmh->buflen, MEM_WAIT);
	bmh->buf = sza->buf;
	bmh->sofar = 0;
	enumerate_vmrs(p, build_maps_cb, bmh);
	return sza;
}

static struct chan *procopen(struct chan *c, int omode)
{
	ERRSTACK(2);
	struct proc *p;
	struct pgrp *pg;
	struct chan *tc;
	int pid;

	if (c->qid.type & QTDIR)
		return devopen(c, omode, 0, 0, procgen);

	if (QID(c->qid) == Qtrace) {
		error(ENOSYS, ERROR_FIXME);
#if 0
		if (omode != OREAD)
			error(EPERM, ERROR_FIXME);
		lock(&tlock);
		if (waserror()) {
			unlock(&tlock);
			nexterror();
		}
		if (topens > 0)
			error(EFAIL, "already open");
		topens++;
		if (tevents == NULL) {
			tevents = (Traceevent *) kzmalloc(sizeof(Traceevent) * Nevents,
											  MEM_WAIT);
			if (tevents == NULL)
				error(ENOMEM, ERROR_FIXME);
			tpids = kzmalloc(Ntracedpids * 20, MEM_WAIT);
			if (tpids == NULL) {
				kfree(tpids);
				tpids = NULL;
				error(ENOMEM, ERROR_FIXME);
			}
			tpidsc = tpids;
			tpidse = tpids + Ntracedpids * 20;
			*tpidsc = 0;
			tproduced = tconsumed = 0;
		}
		proctrace = _proctrace;
		poperror();
		unlock(&tlock);

		c->mode = openmode(omode);
		c->flag |= COPEN;
		c->offset = 0;
		return c;
#endif
	}
	if (QID(c->qid) == Qtracepids) {
		error(ENOSYS, ERROR_FIXME);
#if 0
		if (omode != OREAD)
			error(EPERM, ERROR_FIXME);
		c->mode = openmode(omode);
		c->flag |= COPEN;
		c->offset = 0;
		return c;
#endif
	}
	if ((p = pid2proc(SLOT(c->qid))) == NULL)
		error(ESRCH, ERROR_FIXME);
	//qlock(&p->debug);
	if (waserror()) {
		//qunlock(&p->debug);
		proc_decref(p);
		nexterror();
	}
	pid = PID(c->qid);
	if (p->pid != pid)
		error(ESRCH, ERROR_FIXME);

	omode = openmode(omode);

	switch (QID(c->qid)) {
		case Qtext:
			error(ENOSYS, ERROR_FIXME);
/*
			if (omode != OREAD)
				error(EPERM, ERROR_FIXME);
			tc = proctext(c, p);
			tc->offset = 0;
			poperror();
			qunlock(&p->debug);
			proc_decref(p);
			cclose(c);
			return tc;
*/
		case Qproc:
		case Qsegment:
		case Qprofile:
		case Qfd:
			if (omode != O_READ)
				error(EPERM, ERROR_FIXME);
			break;

		case Qnote:
//          if (p->privatemem)
			error(EPERM, ERROR_FIXME);
			break;

		case Qmem:
//          if (p->privatemem)
			error(EPERM, ERROR_FIXME);
			//nonone(p);
			break;

		case Qargs:
		case Qnoteid:
		case Qwait:
		case Qregs:
		case Qfpregs:
		case Qkregs:
		case Qsyscall:
		case Qcore:
			nonone(p);
			break;

		case Qns:
			if (omode != O_READ)
				error(EPERM, ERROR_FIXME);
			c->aux = kzmalloc(sizeof(struct mntwalk), MEM_WAIT);
			break;
		case Quser:
		case Qstatus:
		case Qvmstatus:
		case Qctl:
			break;

		case Qstrace:
			if (!p->strace)
				error(ENOENT, "Process does not have tracing enabled");
			spin_lock(&p->strace->lock);
			if (p->strace->tracing) {
				spin_unlock(&p->strace->lock);
				error(EBUSY, "Process is already being traced");
			}
			/* It's not critical that we reopen before setting tracing, but it's
			 * a little cleaner (concurrent syscalls could be trying to use the
			 * queue before it was reopened, and they'd throw). */
			qreopen(p->strace->q);
			p->strace->tracing = TRUE;
			spin_unlock(&p->strace->lock);
			/* the ref we are upping is the one we put in __proc_free, which is
			 * the one we got from CMstrace{on,me}.  We have a ref on p, so we
			 * know we won't free until we decref the proc. */
			kref_get(&p->strace->users, 1);
			c->aux = p->strace;
			break;
		case Qstrace_traceset:
			if (!p->strace)
				error(ENOENT, "Process does not have tracing enabled");
			kref_get(&p->strace->users, 1);
			c->aux = p->strace;
			break;
		case Qmaps:
			c->aux = build_maps(p);
			break;
		case Qnotepg:
			error(ENOSYS, ERROR_FIXME);
#if 0
			nonone(p);
			pg = p->pgrp;
			if (pg == NULL)
				error(ESRCH, ERROR_FIXME);
			if (omode != OWRITE || pg->pgrpid == 1)
				error(EPERM, ERROR_FIXME);
			c->pgrpid.path = pg->pgrpid + 1;
			c->pgrpid.vers = p->noteid;
#endif
			break;

		default:
			printk("procopen %#llux\n", c->qid.path);
			error(EINVAL, ERROR_FIXME);
	}

	/* Affix pid to qid */
//  if (p->state != Dead)
	c->qid.vers = p->pid;
	/* make sure the process slot didn't get reallocated while we were playing */
	//coherence();
	/* TODO: think about what we really want here.  In akaros, we wouldn't have
	 * our pid changed like that. */
	if (p->pid != pid)
		error(ESRCH, ERROR_FIXME);

	tc = devopen(c, omode, 0, 0, procgen);
	poperror();
	//qunlock(&p->debug);
	proc_decref(p);
	return tc;
}

static size_t procwstat(struct chan *c, uint8_t *db, size_t n)
{
	ERRSTACK(2);
	error(ENOSYS, ERROR_FIXME);
#if 0
	struct proc *p;
	struct dir *d;

	if (c->qid.type & QTDIR)
		error(EPERM, ERROR_FIXME);

	if (QID(c->qid) == Qtrace)
		return devwstat(c, db, n);

	if ((p = pid2proc(SLOT(c->qid))) == NULL)
		error(ESRCH, ERROR_FIXME);
	nonone(p);
	d = NULL;
	qlock(&p->debug);
	if (waserror()) {
		qunlock(&p->debug);
		proc_decref(p);
		kfree(d);
		nexterror();
	}

	if (p->pid != PID(c->qid))
		error(ESRCH, ERROR_FIXME);

	if (strcmp(current->user.name, p->user.name) != 0 && !iseve())
		error(EPERM, ERROR_FIXME);

	d = kzmalloc(sizeof(struct dir) + n, MEM_WAIT);
	n = convM2D(db, n, &d[0], (char *)&d[1]);
	if (n == 0)
		error(ENOENT, ERROR_FIXME);
	if (!emptystr(d->uid) && strcmp(d->uid, p->user.name) != 0) {
		if (!iseve())
			error(EPERM, ERROR_FIXME);
		else
			proc_set_username(p, d->uid);
	}
	if (d->mode != -1)
		p->procmode = d->mode & 0777;

	poperror();
	qunlock(&p->debug);
	proc_decref(p);
	kfree(d);

	return n;
#endif
}

#if 0
static long procoffset(long offset, char *va, int *np)
{
	if (offset > 0) {
		offset -= *np;
		if (offset < 0) {
			memmove(va, va + *np + offset, -offset);
			*np = -offset;
		} else
			*np = 0;
	}
	return offset;
}

static int procqidwidth(struct chan *c)
{
	char buf[32];

	return sprint(buf, "%lu", c->qid.vers);
}

int procfdprint(struct chan *c, int fd, int w, char *s, int ns)
{
	int n;

	if (w == 0)
		w = procqidwidth(c);
	n = snprint(s, ns,
				"%3d %.2s %C %4ud (%.16llux %*lud %.2ux) %5ld %8lld %s\n", fd,
				&"r w rw"[(c->mode & 3) << 1], c->dev->dc, c->devno,
				c->qid.path, w, c->qid.vers, c->qid.type, c->iounit, c->offset,
				c->name->s);
	return n;
}

static int procfds(struct proc *p, char *va, int count, long offset)
{
	ERRSTACK(2);
	struct fgrp *f;
	struct chan *c;
	char buf[256];
	int n, i, w, ww;
	char *a;

	/* print to buf to avoid holding fgrp lock while writing to user space */
	if (count > sizeof buf)
		count = sizeof buf;
	a = buf;

	qlock(&p->debug);
	f = p->fgrp;
	if (f == NULL) {
		qunlock(&p->debug);
		return 0;
	}
	lock(f);
	if (waserror()) {
		unlock(f);
		qunlock(&p->debug);
		nexterror();
	}

	n = readstr(0, a, count, p->dot->name->s);
	n += snprint(a + n, count - n, "\n");
	offset = procoffset(offset, a, &n);
	/* compute width of qid.path */
	w = 0;
	for (i = 0; i <= f->maxfd; i++) {
		c = f->fd[i];
		if (c == NULL)
			continue;
		ww = procqidwidth(c);
		if (ww > w)
			w = ww;
	}
	for (i = 0; i <= f->maxfd; i++) {
		c = f->fd[i];
		if (c == NULL)
			continue;
		n += procfdprint(c, i, w, a + n, count - n);
		offset = procoffset(offset, a, &n);
	}
	poperror();
	unlock(f);
	qunlock(&p->debug);

	/* copy result to user space, now that locks are released */
	memmove(va, buf, n);

	return n;
}
#endif
static void procclose(struct chan *c)
{
	if (QID(c->qid) == Qtrace) {
		spin_lock(&tlock);
		if (topens > 0)
			topens--;
		/* ??
		   if(topens == 0)
		   proctrace = notrace;
		 */
		spin_unlock(&tlock);
	}
	if (QID(c->qid) == Qsyscall) {
		if (c->aux)
			qclose(c->aux);
		c->aux = NULL;
	}
	if (QID(c->qid) == Qns && c->aux != 0)
		kfree(c->aux);
	if (QID(c->qid) == Qmaps && c->aux != 0)
		kfree(c->aux);
	if (QID(c->qid) == Qstrace && c->aux != 0) {
		struct strace *s = c->aux;

		assert(c->flag & COPEN);	/* only way aux should have been set */
		s->tracing = FALSE;
		qhangup(s->q, NULL);
		kref_put(&s->users);
		c->aux = NULL;
	}
	if (QID(c->qid) == Qstrace_traceset && c->aux != 0) {
		struct strace *s = c->aux;

		assert(c->flag & COPEN);
		kref_put(&s->users);
		c->aux = NULL;
	}
}

void int2flag(int flag, char *s)
{
	if (flag == 0) {
		*s = '\0';
		return;
	}
	*s++ = '-';
	if (flag & MAFTER)
		*s++ = 'a';
	if (flag & MBEFORE)
		*s++ = 'b';
	if (flag & MCREATE)
		*s++ = 'c';
	if (flag & MCACHE)
		*s++ = 'C';
	*s = '\0';
}

#if 0
static char *argcpy(char *s, char *p)
{
	char *t, *tp, *te;
	int n;

	n = p - s;
	if (n > 128)
		n = 128;
	if (n <= 0) {
		t = kzmalloc(1, MEM_WAIT);
		*t = 0;
		return t;
	}
	t = kzmalloc(n, MEM_WAIT);
	tp = t;
	te = t + n;

	while (tp + 1 < te) {
		for (p--; p > s && p[-1] != 0; p--) ;
		tp = seprint(tp, te, "%q ", p);
		if (p == s)
			break;
	}
	if (*tp == ' ')
		*tp = 0;
	return t;
}

static int procargs(struct proc *p, char *buf, int nbuf)
{
	char *s;

	if (p->setargs == 0) {
		s = argcpy(p->args, p->args + p->nargs);
		kfree(p->args);
		p->nargs = strlen(s);
		p->args = s;
		p->setargs = 1;
	}
	return snprint(buf, nbuf, "%s", p->args);
}

static int eventsavailable(void *)
{
	return tproduced > tconsumed;
}
#endif

static size_t procread(struct chan *c, void *va, size_t n, off64_t off)
{
	ERRSTACK(1);
	struct proc *p;
	long l, r;
	int i, j, navail, pid, rsize;
	char flag[10], *sps, *srv;
	uintptr_t offset, u;
	int tesz;
	uint8_t *rptr;
	struct mntwalk *mw;
	struct strace *s;
	struct sized_alloc *sza;

	if (c->qid.type & QTDIR) {
		int nn;
		printd("procread: dir\n");
		nn = devdirread(c, va, n, 0, 0, procgen);
		printd("procread: %d\n", nn);
		return nn;
	}

	offset = off;
	/* Some shit in proc doesn't need to grab the reference.  For strace, we
	 * already have the chan open, and all we want to do is read the queue,
	 * which exists because of our kref on it. */
	switch (QID(c->qid)) {
	case Qstrace:
		s = c->aux;
		n = qread(s->q, va, n);
		return n;
	case Qstrace_traceset:
		s = c->aux;
		return readmem(offset, va, n, s->trace_set,
		               bitmap_size(MAX_SYSCALL_NR));
	}

	if ((p = pid2proc(SLOT(c->qid))) == NULL)
		error(ESRCH, "%d: no such process", SLOT(c->qid));
	if (p->pid != PID(c->qid)) {
		proc_decref(p);
		error(ESRCH, "weird: p->pid is %d, PID(c->qid) is %d: mismatch",
		      p->pid, PID(c->qid));
	}
	switch (QID(c->qid)) {
		default:
			proc_decref(p);
			break;
		case Quser: {
				int i;

				i = readstr(off, va, n, p->user.name);
				proc_decref(p);
				return i;
			}
		case Qstatus:{
				/* the old code grew the stack and was hideous.
				 * status is not a high frequency operation; just malloc. */
				char *buf = kmalloc(4096, MEM_WAIT);
				char *s = buf, *e = buf + 4096;
				int i;

				s = seprintf(s, e,
				         "%8d %-*s %-10s %6d", p->pid, PROC_PROGNAME_SZ,
				         p->progname, procstate2str(p->state),
				         p->ppid);
				if (p->strace)
					s = seprintf(s, e, " %d trace users %d traced procs",
					             kref_refcnt(&p->strace->users),
					             kref_refcnt(&p->strace->procs));
				proc_decref(p);
				i = readstr(off, va, n, buf);
				kfree(buf);
				return i;
			}

		case Qvmstatus:
			{
				size_t buflen = 50 * 65 + 2;
				char *buf = kmalloc(buflen, MEM_WAIT);
				int i, offset;
				offset = 0;
				offset += snprintf(buf + offset, buflen - offset, "{\n");
				for (i = 0; i < 65; i++) {
					if (p->vmm.vmexits[i] != 0) {
						offset += snprintf(buf + offset, buflen - offset,
						                   "\"%s\":\"%lld\",\n",
						                   VMX_EXIT_REASON_NAMES[i],
						                   p->vmm.vmexits[i]);
					}
				}
				offset += snprintf(buf + offset, buflen - offset, "}\n");
				proc_decref(p);
				n = readstr(off, va, n, buf);
				kfree(buf);
				return n;
			}
		case Qns:
			//qlock(&p->debug);
			if (waserror()) {
				//qunlock(&p->debug);
				proc_decref(p);
				nexterror();
			}
			if (p->pgrp == NULL || p->pid != PID(c->qid))
				error(ESRCH, ERROR_FIXME);
			mw = c->aux;
			if (mw->cddone) {
				poperror();
				//qunlock(&p->debug);
				proc_decref(p);
				return 0;
			}
			mntscan(mw, p);
			if (mw->mh == 0) {
				mw->cddone = 1;
				i = snprintf(va, n, "cd %s\n", p->dot->name->s);
				poperror();
				//qunlock(&p->debug);
				proc_decref(p);
				return i;
			}
			int2flag(mw->cm->mflag, flag);
			if (strcmp(mw->cm->to->name->s, "#M") == 0) {
				srv = srvname(mw->cm->to->mchan);
				i = snprintf(va, n, "mount %s %s %s %s\n", flag,
							 srv == NULL ? mw->cm->to->mchan->name->s : srv,
							 mw->mh->from->name->s,
							 mw->cm->spec ? mw->cm->spec : "");
				kfree(srv);
			} else
				i = snprintf(va, n, "bind %s %s %s\n", flag,
							 mw->cm->to->name->s, mw->mh->from->name->s);
			poperror();
			//qunlock(&p->debug);
			proc_decref(p);
			return i;
		case Qmaps:
			sza = c->aux;
			i = readmem(off, va, n, sza->buf, sza->size);
			proc_decref(p);
			return i;
	}
	error(EINVAL, "QID %d did not match any QIDs for #proc", QID(c->qid));
	return 0;	/* not reached */
}

static void mntscan(struct mntwalk *mw, struct proc *p)
{
	struct pgrp *pg;
	struct mount *t;
	struct mhead *f;
	int best, i, last, nxt;

	pg = p->pgrp;
	rlock(&pg->ns);

	nxt = 0;
	best = (int)(~0U >> 1);	/* largest 2's complement int */

	last = 0;
	if (mw->mh)
		last = mw->cm->mountid;

	for (i = 0; i < MNTHASH; i++) {
		for (f = pg->mnthash[i]; f; f = f->hash) {
			for (t = f->mount; t; t = t->next) {
				if (mw->mh == 0 || (t->mountid > last && t->mountid < best)) {
					mw->cm = t;
					mw->mh = f;
					best = mw->cm->mountid;
					nxt = 1;
				}
			}
		}
	}
	if (nxt == 0)
		mw->mh = 0;

	runlock(&pg->ns);
}

static size_t procwrite(struct chan *c, void *va, size_t n, off64_t off)
{
	ERRSTACK(2);

	struct proc *p, *t;
	int i, id, l;
	char *args;
	uintptr_t offset = off;
	struct strace *s;

	if (c->qid.type & QTDIR)
		error(EISDIR, ERROR_FIXME);

	if ((p = pid2proc(SLOT(c->qid))) == NULL)
		error(ESRCH, ERROR_FIXME);

	if (waserror()) {
		proc_decref(p);
		nexterror();
	}
	if (p->pid != PID(c->qid))
		error(ESRCH, ERROR_FIXME);

	offset = off;

	switch (QID(c->qid)) {
#if 0
		case Qargs:
			if (n == 0)
				error(EINVAL, ERROR_FIXME);
			if (n >= sizeof buf - strlen(p->text) - 1)
				error(E2BIG, ERROR_FIXME);
			l = snprintf(buf, sizeof buf, "%s [%s]", p->text, (char *)va);
			args = kzmalloc(l + 1, MEM_WAIT);
			if (args == NULL)
				error(ENOMEM, ERROR_FIXME);
			memmove(args, buf, l);
			args[l] = 0;
			kfree(p->args);
			p->nargs = l;
			p->args = args;
			p->setargs = 1;
			break;

		case Qmem:
			if (p->state != Stopped)
				error(EINVAL, ERROR_FIXME);

			n = procctlmemio(p, offset, n, va, 0);
			break;

		case Qregs:
			if (offset >= sizeof(Ureg))
				n = 0;
			else if (offset + n > sizeof(Ureg))
				n = sizeof(Ureg) - offset;
			if (p->dbgreg == 0)
				error(ENODATA, ERROR_FIXME);
			setregisters(p->dbgreg, (char *)(p->dbgreg) + offset, va, n);
			break;

		case Qfpregs:
			n = fpudevprocio(p, va, n, offset, 1);
			break;
#endif
		case Qctl:
			procctlreq(p, va, n);
			break;
		case Qstrace_traceset:
			s = c->aux;
			if (n + offset > bitmap_size(MAX_SYSCALL_NR))
				error(EINVAL, "strace_traceset: Short write (%llu at off %llu)",
				      n, offset);
			if (memcpy_from_user(current, (void*)s->trace_set + offset, va, n))
				error(EFAULT, "strace_traceset: Bad addr (%p + %llu)", va, n);
			break;
		default:
			error(EFAIL, "unknown qid %#llux in procwrite\n", c->qid.path);
	}
	poperror();
	proc_decref(p);
	return n;
}

struct dev procdevtab __devtab = {
	.name = "proc",

	.reset = devreset,
	.init = procinit,
	.shutdown = devshutdown,
	.attach = procattach,
	.walk = procwalk,
	.stat = procstat,
	.open = procopen,
	.create = devcreate,
	.close = procclose,
	.read = procread,
	.bread = devbread,
	.write = procwrite,
	.bwrite = devbwrite,
	.remove = devremove,
	.wstat = procwstat,
	.power = devpower,
	.chaninfo = devchaninfo,
};

#if 0
static struct chan *proctext(struct chan *c, struct proc *p)
{
	ERRSTACK(2);
	struct chan *tc;
	Image *i;
	Segment *s;

	s = p->seg[TSEG];
	if (s == 0)
		error(ENOENT, ERROR_FIXME);
	if (p->state == Dead)
		error(ESRCH, ERROR_FIXME);

	lock(s);
	i = s->image;
	if (i == 0) {
		unlock(s);
		error(ESRCH, ERROR_FIXME);
	}
	unlock(s);

	lock(i);
	if (waserror()) {
		unlock(i);
		nexterror();
	}

	tc = i->c;
	if (tc == 0)
		error(ESRCH, ERROR_FIXME);

	/* TODO: what do you want here?  you can't get a kref and have the new val
	 * be 1.  Here is the old code: if (kref_get(&tc->ref, 1) == 1 || ... ) */
	if (kref_refcnt(&tc->ref, 1) == 1 || (tc->flag & COPEN) == 0
		|| tc->mode != OREAD) {
		cclose(tc);
		error(ESRCH, ERROR_FIXME);
	}

	if (p->pid != PID(c->qid)) {
		cclose(tc);
		error(ESRCH, ERROR_FIXME);
	}

	poperror();
	unlock(i);

	return tc;
}

/* TODO: this will fail at compile time, since we don't have a proc-wide rendez,
 * among other things, and we'll need to rewrite this for akaros */
void procstopwait(struct proc *p, int ctl)
{
	ERRSTACK(2);
	int pid;

	if (p->pdbg)
		error(EBUSY, ERROR_FIXME);
	if (procstopped(p) || p->state == Broken)
		return;

	if (ctl != 0)
		p->procctl = ctl;
	p->pdbg = up;
	pid = p->pid;
	qunlock(&p->debug);
	current->psstate = "Stopwait";
	if (waserror()) {
		p->pdbg = 0;
		qlock(&p->debug);
		nexterror();
	}
	rendez_sleep(&current->sleep, procstopped, p);
	poperror();
	qlock(&p->debug);
	if (p->pid != pid)
		error(ESRCH, ERROR_FIXME);
}

#endif
static void procctlcloseone(struct proc *p, int fd)
{
// TODO: resolve this and sys_close
	struct file *file = get_file_from_fd(&p->open_files, fd);
	int retval = 0;
	printd("%s %d\n", __func__, fd);
	/* VFS */
	if (file) {
		put_file_from_fd(&p->open_files, fd);
		kref_put(&file->f_kref);	/* Drop the ref from get_file */
		return;
	}
	/* 9ns, should also handle errors (bad FD, etc) */
	retval = sysclose(fd);
	return;

	//sys_close(p, fd);
}

void procctlclosefiles(struct proc *p, int all, int fd)
{
	int i;

	if (all)
		for (i = 0; i < NR_FILE_DESC_MAX; i++)
			procctlcloseone(p, i);
	else
		procctlcloseone(p, fd);
}

static void strace_shutdown(struct kref *a)
{
	struct strace *strace = container_of(a, struct strace, procs);
	static const char base_msg[] = "# Traced ~%lu syscs, Dropped %lu";
	size_t msg_len = NUMSIZE64 * 2 + sizeof(base_msg);
	char *msg = kmalloc(msg_len, 0);

	if (msg)
		snprintf(msg, msg_len, base_msg, strace->appx_nr_sysc,
		         atomic_read(&strace->nr_drops));
	qhangup(strace->q, msg);
	kfree(msg);
}

static void strace_release(struct kref *a)
{
	struct strace *strace = container_of(a, struct strace, users);

	qfree(strace->q);
	kfree(strace);
}

static void procctlreq(struct proc *p, char *va, int n)
{
	ERRSTACK(1);
	int8_t irq_state = 0;
	int npc, pri, core;
	struct cmdbuf *cb;
	struct cmdtab *ct;
	int64_t time;
	char *e;
	struct strace *strace;

	cb = parsecmd(va, n);
	if (waserror()) {
		kfree(cb);
		nexterror();
	}

	ct = lookupcmd(cb, proccmd, ARRAY_SIZE(proccmd));

	switch (ct->index) {
	case CMstraceall:
	case CMstraceme:
	case CMstrace_drop:
		/* common allocation.  if we inherited, we might have one already */
		if (!p->strace) {
			strace = kzmalloc(sizeof(*p->strace), MEM_WAIT);
			spinlock_init(&strace->lock);
			bitmap_set(strace->trace_set, 0, MAX_SYSCALL_NR);
			strace->q = qopen(65536, Qmsg, NULL, NULL);
			/* The queue is reopened and hungup whenever we open the Qstrace
			 * file.  This hangup might not be necessary, but is safer. */
			qhangup(strace->q, NULL);
			/* both of these refs are put when the proc is freed.  procs is for
			 * every process that has this p->strace.  users is procs + every
			 * user (e.g. from open()).
			 *
			 * it is possible to kref_put the procs kref in proc_destroy, which
			 * would make strace's job easier (no need to do an async wait on
			 * the child), and we wouldn't need to decref p in
			 * procread(Qstrace).  But the downside is that proc_destroy races
			 * with us here with the kref initialization. */
			kref_init(&strace->procs, strace_shutdown, 1);
			kref_init(&strace->users, strace_release, 1);
			if (!atomic_cas_ptr((void**)&p->strace, 0, strace)) {
				/* someone else won the race and installed strace. */
				qfree(strace->q);
				kfree(strace);
				error(EAGAIN, "Concurrent strace init, try again");
			}
		}
		break;
	}

	/* actually do the command. */
	switch (ct->index) {
	default:
		error(EFAIL, "Command not implemented");
		break;
	case CMclose:
		procctlclosefiles(p, 0, atoi(cb->f[1]));
		break;
	case CMclosefiles:
		procctlclosefiles(p, 1, 0);
		break;
#if 0
		we may want this.Let us pause a proc.case CMhang:p->hang = 1;
		break;
#endif
	case CMstraceme:
		p->strace->inherit = FALSE;
		break;
	case CMstraceall:
		p->strace->inherit = TRUE;
		break;
	case CMstrace_drop:
		if (!strcmp(cb->f[1], "on"))
			p->strace->drop_overflow = TRUE;
		else if (!strcmp(cb->f[1], "off"))
			p->strace->drop_overflow = FALSE;
		else
			error(EINVAL, "strace_drop takes on|off %s", cb->f[1]);
		break;
	}
	poperror();
	kfree(cb);
}

#if 0
static int procstopped(void *a)
{
	struct proc *p = a;
	return p->state == Stopped;
}

static int
procctlmemio(struct proc *p, uintptr_t offset, int n, void *va, int read)
{
	KMap *k;
	Pte *pte;
	Page *pg;
	Segment *s;
	uintptr_t soff, l;			/* hmmmm */
	uint8_t *b;
	uintmem pgsz;

	for (;;) {
		s = seg(p, offset, 1);
		if (s == 0)
			error(EINVAL, ERROR_FIXME);

		if (offset + n >= s->top)
			n = s->top - offset;

		if (!read && (s->type & SG_TYPE) == SG_TEXT)
			s = txt2data(p, s);

		s->steal++;
		soff = offset - s->base;
		if (waserror()) {
			s->steal--;
			nexterror();
		}
		if (fixfault(s, offset, read, 0, s->color) == 0)
			break;
		poperror();
		s->steal--;
	}
	poperror();
	pte = s->map[soff / PTEMAPMEM];
	if (pte == 0)
		panic("procctlmemio");
	pgsz = m->pgsz[s->pgszi];
	pg = pte->pages[(soff & (PTEMAPMEM - 1)) / pgsz];
	if (pagedout(pg))
		panic("procctlmemio1");

	l = pgsz - (offset & (pgsz - 1));
	if (n > l)
		n = l;

	k = kmap(pg);
	if (waserror()) {
		s->steal--;
		kunmap(k);
		nexterror();
	}
	b = (uint8_t *) VA(k);
	b += offset & (pgsz - 1);
	if (read == 1)
		memmove(va, b, n);	/* This can fault */
	else
		memmove(b, va, n);
	poperror();
	kunmap(k);

	/* Ensure the process sees text page changes */
	if (s->flushme)
		memset(pg->cachectl, PG_TXTFLUSH, sizeof(pg->cachectl));

	s->steal--;

	if (read == 0)
		p->newtlb = 1;

	return n;
}

static Segment *txt2data(struct proc *p, Segment * s)
{
	int i;
	Segment *ps;

	ps = newseg(SG_DATA, s->base, s->size);
	ps->image = s->image;
	kref_get(&ps->image->ref, 1);
	ps->fstart = s->fstart;
	ps->flen = s->flen;
	ps->flushme = 1;

	qlock(&p->seglock);
	for (i = 0; i < NSEG; i++)
		if (p->seg[i] == s)
			break;
	if (i == NSEG)
		panic("segment gone");

	qunlock(&s->lk);
	putseg(s);
	qlock(&ps->lk);
	p->seg[i] = ps;
	qunlock(&p->seglock);

	return ps;
}

Segment *data2txt(Segment * s)
{
	Segment *ps;

	ps = newseg(SG_TEXT, s->base, s->size);
	ps->image = s->image;
	kref_get(&ps->image->ref, 1);
	ps->fstart = s->fstart;
	ps->flen = s->flen;
	ps->flushme = 1;

	return ps;
}
#endif
