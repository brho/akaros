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

static int growfd(struct fgrp *f, int fd)
{
	int n;
	struct chan **nfd, **ofd;

	if (fd < f->nfd)
		return 0;
	n = f->nfd + DELTAFD;
	if (n > MAXNFD)
		n = MAXNFD;
	if (fd >= n)
		return -1;
	nfd = kzmalloc(n * sizeof(struct chan *), 0);
	if (nfd == NULL)
		return -1;
	ofd = f->fd;
	memmove(nfd, ofd, f->nfd * sizeof(struct chan *));
	f->fd = nfd;
	f->nfd = n;
	kfree(ofd);
	return 0;
}

int newfd(struct chan *c)
{
	int i;
	struct fgrp *f = current->fgrp;

	spin_lock(&f->lock);
	if (f->closed) {
		spin_unlock(&f->lock);
		return -1;
	}
	/* VFS hack */
	/* We'd like to ask it to start at f->minfd, but that would require us to
	 * know if we closed anything.  Since we share the FD numbers with the VFS,
	 * there is no way to know that. */
	i = get_fd(&current->open_files, 0);
	while (i >= f->nfd) {
		if (growfd(f, i) < 0) {
			spin_unlock(&f->lock);
			exhausted("file descriptors");
			return -1;
		}
		cpu_relax();
	}
	assert(f->fd[i] == 0);
#if 0	// 9ns style
	/* TODO: use a unique integer allocator */
	for (i = f->minfd; i < f->nfd; i++)
		if (f->fd[i] == 0)
			break;
	if (i >= f->nfd && growfd(f, i) < 0) {
		spin_unlock(&f->lock);
		exhausted("file descriptors");
		return -1;
	}
#endif
	f->minfd = i + 1;
	if (i > f->maxfd)
		f->maxfd = i;
	f->fd[i] = c;
	spin_unlock(&f->lock);
	return i;
}

struct chan *fdtochan(struct fgrp *f, int fd, int mode, int chkmnt, int iref)
{
	struct chan *c;

	c = 0;

	spin_lock(&f->lock);
	if (f->closed) {
		spin_unlock(&f->lock);
		error("File group closed");
	}
	if (fd < 0 || f->maxfd < fd || (c = f->fd[fd]) == 0) {
		spin_unlock(&f->lock);
		error(Ebadfd);
	}
	if (iref)
		kref_get(&c->ref, 1);
	spin_unlock(&f->lock);

	if (chkmnt && (c->flag & CMSG)) {
		if (iref)
			cclose(c);
		error(Ebadusefd);
	}

	if (mode < 0 || c->mode == ORDWR)
		return c;

	if ((mode & OTRUNC) && IS_RDONLY(c->mode)) {
		if (iref)
			cclose(c);
		error(Ebadusefd);
	}

	/* TODO: this is probably wrong.  if you get this from a dev, in the dev's
	 * open, you are probably saving mode directly, without passing it through
	 * openmode. */
	if ((mode & ~OTRUNC) != c->mode) {
		warn("Trunc mode issue: mode %o, mode minus trunc %o, chan mode %o\n",
			 mode, mode & ~OTRUNC, c->mode);
		if (iref)
			cclose(c);
		error(Ebadusefd);
	}

	return c;
}

long kchanio(void *vc, void *buf, int n, int mode)
{
	ERRSTACK(1);
	int r;
	struct chan *c;

	c = vc;
	if (waserror()) {
		poperror();
		return -1;
	}

	if (IS_RDONLY(mode))
		r = devtab[c->type].read(c, buf, n, c->offset);
	else
		r = devtab[c->type].write(c, buf, n, c->offset);

	spin_lock(&c->lock);
	c->offset += r;
	spin_unlock(&c->lock);
	poperror();
	return r;
}

int openmode(uint32_t omode)
{
#if 0
	/* this is the old plan9 style.  i think they want to turn exec into read,
	 * and strip off anything higher, and just return the RD/WR style bits.  not
	 * stuff like ORCLOSE.  the lack of OEXCL might be a bug on their part (it's
	 * the only one of their non-RW-related flags that isn't masked out) */
	if (o >= (OTRUNC | OCEXEC | ORCLOSE | OEXEC))
		error(Ebadarg);
	o &= ~(OTRUNC | OCEXEC | ORCLOSE);
	if (o > OEXEC)
		error(Ebadarg);
	if (o == OEXEC)
		return OREAD;
	return o;
#endif
	/* no error checking (we have a shitload of flags anyway), and we return the
	 * basic access modes (RD/WR/ETC) */
	if (omode == O_EXEC)
		return O_RDONLY;
	return omode & O_ACCMODE;
}

void fdclose(struct fgrp *f, int fd)
{
	int i;
	struct chan *c;

	spin_lock(&f->lock);
	if (f->closed) {
		spin_unlock(&f->lock);
		return;
	}
	c = f->fd[fd];
	if (c == 0) {
		/* can happen for users with shared fd tables */
		spin_unlock(&f->lock);
		return;
	}
	f->fd[fd] = 0;
	if (fd == f->maxfd)
		for (i = fd; --i >= 0 && f->fd[i] == 0;)
			f->maxfd = i;
	if (fd < f->minfd)
		f->minfd = fd;
	/* VFS hack: give the FD back to VFS */
	put_fd(&current->open_files, fd);
	spin_unlock(&f->lock);
	cclose(c);
}

int syschdir(char *path)
{
	ERRSTACK(1);
	struct chan *c;
	struct pgrp *pg;

	if (waserror()) {
		poperror();
		return -1;
	}

	c = namec(path, Atodir, 0, 0);
	pg = current->pgrp;
	cclose(pg->dot);
	pg->dot = c;
	poperror();
	return 0;
}

int fgrpclose(struct fgrp *f, int fd)
{
	ERRSTACK(1);
	if (waserror()) {
		poperror();
		return -1;
	}

	/*
	 * Take no reference on the chan because we don't really need the
	 * data structure, and are calling fdtochan only for error checks.
	 * fdclose takes care of processes racing through here.
	 */
	fdtochan(f, fd, -1, 0, 0);
	fdclose(f, fd);
	poperror();
	return 0;
}

int sysclose(int fd)
{
	return fgrpclose(current->fgrp, fd);
}

int syscreate(char *path, int mode, uint32_t perm)
{
	ERRSTACK(2);
	int fd;
	struct chan *c;

	if (waserror()) {
		poperror();
		return -1;
	}

	openmode(mode & ~OEXCL);	/* error check only; OEXCL okay here */
	c = namec(path, Acreate, mode, perm);
	if (waserror()) {
		cclose(c);
		nexterror();
	}
	fd = newfd(c);
	if (fd < 0)
		error(Enofd);
	poperror();

	poperror();
	return fd;
}

int sysdup(int old, int new)
{
	ERRSTACK(2);
	int fd;
	struct chan *c, *oc;
	struct fgrp *f = current->fgrp;

	if (waserror()) {
		poperror();
		return -1;
	}

	c = fdtochan(current->fgrp, old, -1, 0, 1);
	if (c->qid.type & QTAUTH)
		error(Eperm);
	fd = new;
	if (fd != -1) {
		/* ideally we'll be done with the VFS before we fix this */
		panic("Need to sync with the VFS");
		spin_lock(&f->lock);
		if (f->closed) {
			spin_unlock(&f->lock);
			return -1;
		}
		if (fd < 0 || growfd(f, fd) < 0) {
			spin_unlock(&f->lock);
			cclose(c);
			error(Ebadfd);
		}
		if (fd > f->maxfd)
			f->maxfd = fd;
		oc = f->fd[fd];
		f->fd[fd] = c;
		spin_unlock(&f->lock);
		if (oc)
			cclose(oc);
	} else {
		if (waserror()) {
			cclose(c);
			nexterror();
		}
		fd = newfd(c);
		if (fd < 0)
			error(Enofd);
		poperror();
	}
	poperror();
	return fd;
}

char *sysfd2path(int fd)
{
	ERRSTACK(1);
	struct chan *c;
	char *s;

	if (waserror()) {
		poperror();
		return NULL;
	}
	c = fdtochan(current->fgrp, fd, -1, 0, 1);
	s = NULL;
	if (c->name != NULL) {
		s = kzmalloc(c->name->len + 1, 0);
		if (s == NULL) {
			cclose(c);
			error(Enomem);
		}
		memmove(s, c->name->s, c->name->len + 1);
		cclose(c);
	}
	poperror();
	return s;
}

int sysfauth(int fd, char *aname)
{
	ERRSTACK(2);
	struct chan *c, *ac;

	if (waserror()) {
		poperror();
		return -1;
	}

	validname(aname, 0);
	c = fdtochan(current->fgrp, fd, ORDWR, 0, 1);
	if (waserror()) {
		cclose(c);
		nexterror();
	}

	ac = mntauth(c, aname);

	/* at this point ac is responsible for keeping c alive */
	poperror();	/* c */
	cclose(c);

	if (waserror()) {
		cclose(ac);
		nexterror();
	}

	fd = newfd(ac);
	if (fd < 0)
		error(Enofd);
	poperror();	/* ac */

	poperror();

	return fd;
}

int sysfversion(int fd, unsigned int msize, char *vers, unsigned int arglen)
{
	ERRSTACK(2);
	int m;
	struct chan *c;

	if (waserror()) {
		poperror();
		return -1;
	}

	/* check there's a NUL in the version string */
	if (arglen == 0 || memchr(vers, 0, arglen) == 0)
		error(Ebadarg);

	c = fdtochan(current->fgrp, fd, ORDWR, 0, 1);
	if (waserror()) {
		cclose(c);
		nexterror();
	}

	m = mntversion(c, vers, msize, arglen);

	poperror();
	cclose(c);

	poperror();
	return m;
}

int syspipe(int fd[2])
{
	ERRSTACK(1);
	struct dev *d;
	struct fgrp *f;
	struct chan *c[2];
	static char *names[] = { "data", "data1" };

	f = current->fgrp;

	d = &devtab[devno('|', 0)];
	c[0] = namec("#|", Atodir, 0, 0);
	c[1] = 0;
	fd[0] = -1;
	fd[1] = -1;
	if (waserror()) {
		if (c[0] != 0)
			cclose(c[0]);
		if (c[1] != 0)
			cclose(c[1]);
		if (fd[0] >= 0) {
			/* VFS hack */
			f->fd[fd[0]] = 0;
			put_fd(&current->open_files, fd[0]);
		}
		if (fd[1] >= 0) {
			/* VFS hack */
			f->fd[fd[1]] = 0;
			put_fd(&current->open_files, fd[1]);
		}
		poperror();
		return -1;
	}
	c[1] = cclone(c[0]);
	if (walk(&c[0], &names[0], 1, 1, NULL) < 0)
		error(Egreg);
	if (walk(&c[1], &names[1], 1, 1, NULL) < 0)
		error(Egreg);
	c[0] = d->open(c[0], ORDWR);
	c[1] = d->open(c[1], ORDWR);
	fd[0] = newfd(c[0]);
	if (fd[0] < 0)
		error(Enofd);
	fd[1] = newfd(c[1]);
	if (fd[1] < 0)
		error(Enofd);
	poperror();
	return 0;
}

int sysfwstat(int fd, uint8_t * buf, int n)
{
	ERRSTACK(2);
	struct chan *c;

	if (waserror()) {
		poperror();
		return -1;
	}

	validstat(buf, n);
	c = fdtochan(current->fgrp, fd, -1, 1, 1);
	if (waserror()) {
		cclose(c);
		nexterror();
	}
	n = devtab[c->type].wstat(c, buf, n);
	poperror();
	cclose(c);

	poperror();
	return n;
}

long bindmount(struct chan *c, char *old, int flag, char *spec)
{
	ERRSTACK(1);
	int ret;
	struct chan *c1;

	if (flag > MMASK || (flag & MORDER) == (MBEFORE | MAFTER))
		error(Ebadarg);

	c1 = namec(old, Amount, 0, 0);
	if (waserror()) {
		cclose(c1);
		nexterror();
	}
	ret = cmount(c, c1, flag, spec);

	poperror();
	cclose(c1);
	return ret;
}

int sysbind(char *new, char *old, int flags)
{
	ERRSTACK(2);
	long r;
	struct chan *c0;

	if (waserror()) {
		poperror();
		return -1;
	}

	c0 = namec(new, Abind, 0, 0);
	if (waserror()) {
		cclose(c0);
		nexterror();
	}
	r = bindmount(c0, old, flags, "");
	poperror();
	cclose(c0);

	poperror();
	return r;
}

int sysmount(int fd, int afd, char *old, int flags, char *spec)
{
	ERRSTACK(1);
	long r;
	volatile struct {
		struct chan *c;
	} c0;
	volatile struct {
		struct chan *c;
	} bc;
	volatile struct {
		struct chan *c;
	} ac;
	struct mntparam mntparam;

	ac.c = NULL;
	bc.c = NULL;
	c0.c = NULL;
	if (waserror()) {
		cclose(ac.c);
		cclose(bc.c);
		cclose(c0.c);
		poperror();
		return -1;
	}
	bc.c = fdtochan(current->fgrp, fd, ORDWR, 0, 1);
	if (afd >= 0)
		ac.c = fdtochan(current->fgrp, afd, ORDWR, 0, 1);
	mntparam.chan = bc.c;
	mntparam.authchan = ac.c;
	mntparam.spec = spec;
	mntparam.flags = flags;
	c0.c = devtab[devno('M', 0)].attach((char *)&mntparam);

	r = bindmount(c0.c, old, flags, spec);
	poperror();
	cclose(ac.c);
	cclose(bc.c);
	cclose(c0.c);

	return r;
}

int sysunmount(char *old, char *new)
{
	ERRSTACK(1);
	volatile struct {
		struct chan *c;
	} cmount;
	volatile struct {
		struct chan *c;
	} cmounted;

	cmount.c = NULL;
	cmounted.c = NULL;
	if (waserror()) {
		cclose(cmount.c);
		cclose(cmounted.c);
		poperror();
		return -1;
	}

	cmount.c = namec(new, Amount, 0, 0);
	if (old != NULL && old[0] != '\0') {
		/*
		 * This has to be namec(..., Aopen, ...) because
		 * if arg[0] is something like /srv/cs or /fd/0,
		 * opening it is the only way to get at the real
		 * Chan underneath.
		 */
		cmounted.c = namec(old, Aopen, OREAD, 0);
	}

	cunmount(cmount.c, cmounted.c);
	poperror();
	cclose(cmount.c);
	cclose(cmounted.c);
	return 0;
}

int sysopen(char *path, int vfs_flags)
{
	ERRSTACK(2);
	int fd;
	struct chan *c;

	if (waserror()) {
		poperror();
		return -1;
	}

	openmode(vfs_flags);	/* error check only */
	c = namec(path, Aopen, vfs_flags, 0);
	if (waserror()) {
		cclose(c);
		nexterror();
	}
	fd = newfd(c);
	if (fd < 0)
		error(Enofd);
	poperror();

	poperror();
	return fd;
}

long unionread(struct chan *c, void *va, long n)
{
	ERRSTACK(1);
	int i;
	long nr;
	struct mhead *m;
	struct mount *mount;

	qlock(&c->umqlock);
	m = c->umh;
	rlock(&m->lock);
	mount = m->mount;
	/* bring mount in sync with c->uri and c->umc */
	for (i = 0; mount != NULL && i < c->uri; i++)
		mount = mount->next;

	nr = 0;
	while (mount != NULL) {
		/* Error causes component of union to be skipped */
		if (mount->to) {
			/* normally we want to discard the error, but for our ghetto kdirent
			 * hack, we need to repeat unionread if we saw a Eshort */
			if (waserror()) {
				if (!strcmp(current_errstr(), Eshort)) {
					runlock(&m->lock);
					qunlock(&c->umqlock);
					nexterror();
				}
				/* poperror done below for either branch */
			} else {
				if (c->umc == NULL) {
					c->umc = cclone(mount->to);
					c->umc = devtab[c->umc->type].open(c->umc, OREAD);
				}

				nr = devtab[c->umc->type].read(c->umc, va, n, c->umc->offset);
				if (nr < 0)
					nr = 0;	/* dev.c can return -1 */
				c->umc->offset += nr;
			}
			poperror();	/* pop regardless */
		}
		if (nr > 0)
			break;

		/* Advance to next element */
		c->uri++;
		if (c->umc) {
			cclose(c->umc);
			c->umc = NULL;
		}
		mount = mount->next;
	}
	runlock(&m->lock);
	qunlock(&c->umqlock);
	return nr;
}

static void unionrewind(struct chan *c)
{
	qlock(&c->umqlock);
	c->uri = 0;
	if (c->umc) {
		cclose(c->umc);
		c->umc = NULL;
	}
	qunlock(&c->umqlock);
}

static long rread(int fd, void *va, long n, int64_t * offp)
{
	ERRSTACK(3);
	int dir;
	struct chan *c;
	int64_t off;

	/* dirty dirent hack */
#define MIN_M_BUF_SZ 58	/* TODO: 59 is the smallest i've seen */
	void *real_va = va;
	void *buf_for_M = 0;
	size_t buf_sz = 0;
	long real_n = n;

	if (waserror()) {
		kfree(buf_for_M);
		poperror();
		return -1;
	}

	c = fdtochan(current->fgrp, fd, OREAD, 1, 1);
	if (waserror()) {
		cclose(c);
		nexterror();
	}

	if (n < 0)
		error(Etoosmall);

	dir = c->qid.type & QTDIR;

	/* dirty kdirent hack: userspace is expecting kdirents, but all of 9ns
	 * produces Ms.  i'm assuming we're only being asked to read a single
	 * dirent, which is usually the case for calls like readdir() (which just
	 * calls read for a single dirent). */
	if (dir)
		assert(n >= sizeof(struct kdirent));
	buf_sz = 2 * MIN_M_BUF_SZ - 1;
	/* We need to read exactly one dirent and avoid reading too much from the
	 * underlying dev, so that subsequent reads don't miss any dirents.  So we
	 * start small, and if our buffer is too small (e.g. for long named
	 * dirents), we increase by a minumum amount.  This way, we'll succeed on
	 * the next invocation, but we won't have enough room for more than one
	 * entry. */
	while (waserror()) {
		/* FYI: this scheme doesn't work with mounts */
		if (!dir || strcmp(current_errstr(), Eshort))
			nexterror();
		buf_sz += MIN_M_BUF_SZ;
		poperror();
	}
	if (dir) {
		if (!buf_for_M)
			buf_for_M = kmalloc(buf_sz, KMALLOC_WAIT);
		else
			buf_for_M = krealloc(buf_for_M, buf_sz, KMALLOC_WAIT);
		va = buf_for_M;
		n = buf_sz;
	}

	/* this is the normal plan9 read */
	if (dir && c->umh)
		n = unionread(c, va, n);
	else {
		if (offp == NULL) {
			spin_lock(&c->lock);	/* lock for int64_t assignment */
			off = c->offset;
			spin_unlock(&c->lock);
		} else
			off = *offp;
		if (off < 0)
			error(Enegoff);
		if (off == 0) {
			if (offp == NULL) {
				spin_lock(&c->lock);
				c->offset = 0;
				c->dri = 0;
				spin_unlock(&c->lock);
			}
			unionrewind(c);
		}
		n = devtab[c->type].read(c, va, n, off);
		spin_lock(&c->lock);
		c->offset += n;
		spin_unlock(&c->lock);
	}

	/* dirty kdirent hack */
	if (dir) {
		convM2kdirent(buf_for_M, buf_sz, real_va, 0);
		kfree(buf_for_M);
	}
	poperror();	/* matching our while(waserror) */

	poperror();
	cclose(c);

	poperror();
	return n;
}

long sysread(int fd, void *va, long n)
{
	return rread(fd, va, n, NULL);
}

long syspread(int fd, void *va, long n, int64_t off)
{
	return rread(fd, va, n, &off);
}

int sysremove(char *path)
{
	ERRSTACK(2);
	struct chan *c;

	if (waserror()) {
		poperror();
		return -1;
	}

	c = namec(path, Aremove, 0, 0);
	if (waserror()) {
		c->type = -1;	/* see below */
		cclose(c);
		nexterror();
	}
	devtab[c->type].remove(c);
	/*
	 * Remove clunks the fid, but we need to recover the Chan
	 * so fake it up.  -1 aborts the dev's close.
	 */
	c->type = -1;
	poperror();
	cclose(c);

	poperror();
	return 0;
}

int64_t sysseek(int fd, int64_t off, int whence)
{
	ERRSTACK(2);
	struct dir *dir;
	struct chan *c;

	if (waserror()) {
		poperror();
		return -1;
	}

	c = fdtochan(current->fgrp, fd, -1, 1, 1);
	if (waserror()) {
		cclose(c);
		nexterror();
	}

	if (devtab[c->type].dc == '|')
		error(Eisstream);

	switch (whence) {
		case 0:
			if (c->qid.type & QTDIR) {
				if (off != 0)
					error(Eisdir);
				unionrewind(c);
			} else if (off < 0)
				error(Enegoff);
			spin_lock(&c->lock);	/* lock for int64_t assignment */
			c->offset = off;
			spin_unlock(&c->lock);
			break;

		case 1:
			if (c->qid.type & QTDIR)
				error(Eisdir);
			spin_lock(&c->lock);	/* lock for read/write update */
			off += c->offset;
			if (off < 0) {
				spin_unlock(&c->lock);
				error(Enegoff);
			}
			c->offset = off;
			spin_unlock(&c->lock);
			break;

		case 2:
			if (c->qid.type & QTDIR)
				error(Eisdir);
			dir = chandirstat(c);
			if (dir == NULL)
				error("internal error: stat error in seek");
			off += dir->length;
			kfree(dir);
			if (off < 0)
				error(Enegoff);
			spin_lock(&c->lock);	/* lock for read/write update */
			c->offset = off;
			spin_unlock(&c->lock);
			break;

		default:
			error(Ebadarg);
			break;
	}
	poperror();
	c->dri = 0;
	cclose(c);
	poperror();
	return off;
}

void validstat(uint8_t * s, int n)
{
	int m;
	char buf[64];

	if (statcheck(s, n) < 0)
		error(Ebadstat);
	/* verify that name entry is acceptable */
	s += STATFIXLEN - 4 * BIT16SZ;	/* location of first string */
	/*
	 * s now points at count for first string.
	 * if it's too long, let the server decide; this is
	 * only for his protection anyway. otherwise
	 * we'd have to allocate and waserror.
	 */
	m = GBIT16(s);
	s += BIT16SZ;
	if (m + 1 > sizeof buf)
		return;
	memmove(buf, s, m);
	buf[m] = '\0';
	/* name could be '/' */
	if (strcmp(buf, "/") != 0)
		validname(buf, 0);
}

int sysfstat(int fd, uint8_t *buf, int n)
{
	ERRSTACK(2);
	struct chan *c;

	if (waserror()) {
		poperror();
		return -1;
	}

	c = fdtochan(current->fgrp, fd, -1, 0, 1);
	if (waserror()) {
		cclose(c);
		nexterror();
	}
	devtab[c->type].stat(c, buf, n);

	poperror();
	cclose(c);

	poperror();
	return n;
}

int sysfstatakaros(int fd, struct kstat *ks)
{
	int n = 4096;
	uint8_t *buf;
	buf = kmalloc(n, KMALLOC_WAIT);
	n = sysfstat(fd, buf, n);
	if (n > 0) {
		convM2kstat(buf, n, ks);
		n = 0;
	}
	kfree(buf);
	return n;
}

int sysstat(char *path, uint8_t *buf, int n)
{
	ERRSTACK(2);
	struct chan *c;

	if (waserror()) {
		poperror();
		return -1;
	}

	c = namec(path, Aaccess, 0, 0);
	if (waserror()) {
		cclose(c);
		nexterror();
	}
	devtab[c->type].stat(c, buf, n);
	poperror();
	cclose(c);

	poperror();

	return n;
}

int sysstatakaros(char *path, struct kstat *ks)
{
	int n = 4096;
	uint8_t *buf;
	buf = kmalloc(n, KMALLOC_WAIT);
	n = sysstat(path, buf, n);
	if (n > 0) {
		convM2kstat(buf, n, ks);
		n = 0;
	}
	kfree(buf);
	return n;
}

static long rwrite(int fd, void *va, long n, int64_t * offp)
{
	ERRSTACK(3);
	struct chan *c;
	struct dir *dir;
	int64_t off;
	long m;

	if (waserror()) {
		poperror();
		return -1;
	}
	c = fdtochan(current->fgrp, fd, OWRITE, 1, 1);
	if (waserror()) {
		cclose(c);
		nexterror();
	}
	if (c->qid.type & QTDIR)
		error(Eisdir);

	if (n < 0)
		error(Etoosmall);

	if (offp == NULL) {
		/* append changes the offset to the end, and even if we fail later, this
		 * change will persist */
		if (c->flag & CAPPEND) {
			dir = chandirstat(c);
			if (!dir)
				error("internal error: stat error in append write");
			spin_lock(&c->lock);	/* legacy lock for int64 assignment */
			c->offset = dir->length;
			spin_unlock(&c->lock);
			kfree(dir);
		}
		spin_lock(&c->lock);
		off = c->offset;
		c->offset += n;
		spin_unlock(&c->lock);
	} else
		off = *offp;

	if (waserror()) {
		if (offp == NULL) {
			spin_lock(&c->lock);
			c->offset -= n;
			spin_unlock(&c->lock);
		}
		nexterror();
	}
	if (off < 0)
		error(Enegoff);
	m = devtab[c->type].write(c, va, n, off);
	poperror();

	if (offp == NULL && m < n) {
		spin_lock(&c->lock);
		c->offset -= n - m;
		spin_unlock(&c->lock);
	}

	poperror();
	cclose(c);

	poperror();
	return n;
}

long syswrite(int fd, void *va, long n)
{
	return rwrite(fd, va, n, NULL);
}

long syspwrite(int fd, void *va, long n, int64_t off)
{
	return rwrite(fd, va, n, &off);
}

int syswstat(char *path, uint8_t * buf, int n)
{
	ERRSTACK(2);
	struct chan *c;

	if (waserror()) {
		poperror();
		return -1;
	}

	validstat(buf, n);
	c = namec(path, Aaccess, 0, 0);
	if (waserror()) {
		cclose(c);
		nexterror();
	}
	n = devtab[c->type].wstat(c, buf, n);
	poperror();
	cclose(c);

	poperror();
	return n;
}

enum {
	DIRSIZE = STATFIXLEN + 32 * 4,
	DIRREADLIM = 2048,	/* should handle the largest reasonable directory entry */
};

struct dir *chandirstat(struct chan *c)
{
	ERRSTACK(1);
	struct dir *d;
	uint8_t *buf;
	int n, nd, i;

	nd = DIRSIZE;
	for (i = 0; i < 2; i++) {	/* should work by the second try */
		d = kzmalloc(sizeof(struct dir) + nd, 0);
		buf = (uint8_t *) & d[1];
		if (waserror()) {
			kfree(d);
			poperror();
			return NULL;
		}
		n = devtab[c->type].stat(c, buf, nd);
		poperror();
		if (n < BIT16SZ) {
			kfree(d);
			return NULL;
		}
		nd = GBIT16((uint8_t *) buf) + BIT16SZ;	/* size needed to store whole stat buffer including count */
		if (nd <= n) {
			convM2D(buf, n, d, (char *)&d[1]);
			return d;
		}
		/* else sizeof(Dir)+nd is plenty */
		kfree(d);
	}
	return NULL;

}

struct dir *sysdirstat(char *name)
{
	ERRSTACK(2);
	struct chan *c;
	struct dir *d;

	if (waserror()) {
		poperror();
		return NULL;
	}

	c = namec(name, Aaccess, 0, 0);
	if (waserror()) {
		cclose(c);
		nexterror();
	}
	d = chandirstat(c);
	poperror();
	cclose(c);

	poperror();
	return d;
}

struct dir *sysdirfstat(int fd)
{
	ERRSTACK(2);
	struct chan *c;
	struct dir *d;

	if (waserror()) {
		poperror();
		return NULL;
	}

	c = fdtochan(current->fgrp, fd, -1, 0, 1);
	if (waserror()) {
		cclose(c);
		nexterror();
	}
	d = chandirstat(c);
	poperror();
	cclose(c);

	poperror();
	return d;
}

int sysdirwstat(char *name, struct dir *dir)
{
	uint8_t *buf;
	int r;

	r = sizeD2M(dir);
	buf = kzmalloc(r, 0);
	convD2M(dir, buf, r);
	r = syswstat(name, buf, r);
	kfree(buf);
	return r < 0 ? r : 0;
}

int sysdirfwstat(int fd, struct dir *dir)
{
	uint8_t *buf;
	int r;

	r = sizeD2M(dir);
	buf = kzmalloc(r, 0);
	convD2M(dir, buf, r);
	r = sysfwstat(fd, buf, r);
	kfree(buf);
	return r < 0 ? r : 0;
}

static long dirpackage(uint8_t * buf, long ts, struct kdirent **d)
{
	char *s;
	long ss, i, n, nn, m = 0;

	*d = NULL;
	if (ts <= 0)
		return ts;

	/*
	 * first find number of all stats, check they look like stats, & size all associated strings
	 */
	ss = 0;
	n = 0;
	for (i = 0; i < ts; i += m) {
		m = BIT16SZ + GBIT16(&buf[i]);
		if (statcheck(&buf[i], m) < 0)
			break;
		ss += m;
		n++;
	}

	if (i != ts)
		error("bad directory format");

	*d = kzmalloc(n * sizeof(**d) + ss, 0);
	if (*d == NULL)
		error(Enomem);

	/*
	 * then convert all buffers
	 */
	s = (char *)*d + n * sizeof(**d);
	nn = 0;
	for (i = 0; i < ts; i += m) {
		m = BIT16SZ + GBIT16((uint8_t *) & buf[i]);
		if (nn >= n || /*convM2D */ convM2kdirent(&buf[i], m, *d + nn, s) != m) {
			kfree(*d);
			*d = NULL;
			error("bad directory entry");
		}
		nn++;
		s += m;
	}

	return nn;
}

long sysdirread(int fd, struct kdirent **d)
{
	ERRSTACK(2);
	uint8_t *buf;
	long ts;

	*d = NULL;
	if (waserror()) {
		poperror();
		return -1;
	}
	buf = kzmalloc(DIRREADLIM, 0);
	if (buf == NULL)
		error(Enomem);
	if (waserror()) {
		kfree(buf);
		nexterror();
	}
	ts = sysread(fd, buf, DIRREADLIM);
	if (ts >= 0)
		ts = dirpackage(buf, ts, d);
	poperror();
	kfree(buf);
	poperror();
	return ts;
}

int sysiounit(int fd)
{
	ERRSTACK(1);
	struct chan *c;
	int n;

	c = fdtochan(current->fgrp, fd, -1, 0, 1);
	if (waserror()) {
		cclose(c);
		poperror();
		return 0;	/* n.b. */
	}
	n = c->iounit;
	poperror();
	cclose(c);
	return n;
}

/* Notes on concurrency:
 * - Can't hold spinlocks while we call cclose, since it might sleep eventually.
 * - We're called from proc_destroy, so we could have concurrent openers trying
 *   to add to the group (other syscalls), hence the "closed" flag.
 * - dot and slash chans are dealt with in proc_free.  its difficult to close
 *   and zero those with concurrent syscalls, since those are a source of krefs.
 * - the memory is freed in proc_free().  need to wait to do it, since we can
 *   have concurrent accesses to fgrp before free.
 * - Once we lock and set closed, no further additions can happen.  To simplify
 *   our closes, we also allow multiple calls to this func (though that should
 *   never happen with the current code). */
void close_9ns_files(struct proc *p, bool only_cloexec)
{
	struct fgrp *f = p->fgrp;

	spin_lock(&f->lock);
	if (f->closed) {
		spin_unlock(&f->lock);
		warn("Unexpected double-close");
		return;
	}
	if (!only_cloexec)
		f->closed = TRUE;
	spin_unlock(&f->lock);

	/* maxfd is a legit val, not a +1 */
	for (int i = 0; i <= f->maxfd; i++) {
		if (!f->fd[i])
			continue;
		if (only_cloexec && !(f->fd[i]->flag & CCEXEC))
			continue;
		cclose(f->fd[i]);
		f->fd[i] = 0;
	}
}

void print_chaninfo(struct chan *c)
{
	char buf[64] = { 0 };
	bool has_dev = c->type != -1;
	printk("Chan pathname: %s, Dev: %s, Devinfo: %s\n",
		   c->name ? c->name->s : "no cname",
		   has_dev ? devtab[c->type].name : "no dev",
		   has_dev ? devtab[c->type].chaninfo(c, buf, sizeof(buf)) : "no info");
	if (!has_dev)
		printk("No dev: intermediate chan? qid.path: %p\n", c->qid.path);
	printk("\n");
}

void print_9ns_files(struct proc *p)
{
	struct fgrp *f = p->fgrp;
	spin_lock(&f->lock);
	printk("9ns files for proc %d:\n", p->pid);
	/* maxfd is a legit val, not a +1 */
	for (int i = 0; i <= f->maxfd; i++) {
		if (!f->fd[i])
			continue;
		printk("\t9fs %d, ", i);
		print_chaninfo(f->fd[i]);
	}
	spin_unlock(&f->lock);
}

/* TODO: 9ns ns inheritance flags: Shared, copied, or empty.  Looks like we're
 * copying the fgrp, and sharing the pgrp. */
int plan9setup(struct proc *new_proc, struct proc *parent)
{
	struct proc *old_current;
	struct kref *new_dot_ref;
	ERRSTACK(1);
	if (waserror()) {
		printk("plan9setup failed, %s\n", current_errstr());
		poperror();
		return -1;
	}
	if (!parent) {
		/* We are probably spawned by the kernel directly, and have no parent to
		 * inherit from.  Be sure to set up fgrp/pgrp before calling namec().
		 *
		 * TODO: One problem is namec wants a current set for things like
		 * genbuf.  So we'll use new_proc for this bootstrapping.  Note
		 * switch_to() also loads the cr3. */
		new_proc->fgrp = newfgrp();
		new_proc->pgrp = newpgrp();
		old_current = switch_to(new_proc);
		new_proc->slash = namec("#r", Atodir, 0, 0);
		if (!new_proc->slash)
			panic("no root device");
		switch_back(new_proc, old_current);
		/* Want the name to be "/" instead of "#r" */
		cnameclose(new_proc->slash->name);
		new_proc->slash->name = newcname("/");
		new_proc->dot = cclone(new_proc->slash);
		poperror();
		return 0;
	}
	/* Copy semantics: do not change this without revisiting proc_destroy,
	 * close_9ns_files, and closefgrp. */
	new_proc->fgrp = dupfgrp(new_proc, parent->fgrp);
	/* Shared semantics */
	kref_get(&parent->pgrp->ref, 1);
	new_proc->pgrp = parent->pgrp;
	/* copy semantics on / and . (doesn't make a lot of sense in akaros o/w) */
	/* / should never disappear while we hold a ref to parent */
	kref_get(&parent->slash->ref, 1);
	new_proc->slash = parent->slash;
	/* dot could change concurrently, and we could fail to gain a ref if whoever
	 * decref'd dot triggered the release.  if that did happen, new_proc->dot
	 * should update and we can try again. */
	while (!(new_dot_ref = kref_get_not_zero(&parent->dot->ref, 1)))
		cpu_relax();
	/* And now, we can't trust parent->dot, and need to determine our dot from
	 * the ref we obtained. */
	new_proc->dot = container_of(new_dot_ref, struct chan, ref);
	poperror();
	return 0;
}

/* Open flags, create modes, access types, file flags, and all that...
 *
 * there are a bunch of things here:
 * 		1) file creation flags (e.g. O_TRUNC)
 * 		2) file status flags (e.g. O_APPEND)
 * 		3) file open modes (e.g. O_RDWR)
 * 		4) file descriptor flags (e.g. CLOEXEC)
 * 		5) file creation mode (e.g. S_IRWXU)
 * the 1-4 are passed in via open's vfs_flags, and the 5 via mode only when
 * O_CREATE is set.
 *
 * file creation flags (1) only matter when creating, but aren't permanent.
 * O_EXCL, O_DIRECTORY, O_TRUNC, etc.
 *
 * file status flags (2) are per struct file/chan.  stuff like O_APPEND,
 * O_ASYNC, etc.  we convert those to an internal flag bit and store in c->flags
 *
 * the open mode (3) matters for a given FD/chan (chan->mode), and should be
 * stored in the chan. (c->mode) stuff like O_RDONLY.
 *
 * the file descriptor flags (4) clearly are in the FD.  note that the same
 * file/chan can be opened by two different FDs, with different flags.  the only
 * one anyone uses is CLOEXEC.  while exec may not last long in akaros, i can
 * imagine similar "never pass to children" flags/meanings.
 *
 * the file creation mode (5) matters for the device's permissions; given this,
 * it should be stored in the device/inode.  ACLs fall under this category.
 *
 * finally, only certain categories can be edited afterwards: file status flags
 * (2), FD flags (4), and file permissions (5).	*/
int fd_getfl(int fd)
{
	ERRSTACK(1);
	struct chan *c;
	int ret;

	if (waserror()) {
		poperror();
		return -1;
	}
	c = fdtochan(current->fgrp, fd, -1, 0, 1);

	ret = c->mode;
	if (c->flag & CAPPEND)
		ret |= O_APPEND;

	cclose(c);
	poperror();
	return ret;
}

int fd_setfl(int fd, int flags)
{
	ERRSTACK(1);
	struct chan *c;

	if (waserror()) {
		poperror();
		return -1;
	}
	c = fdtochan(current->fgrp, fd, -1, 0, 1);

	if (flags & O_APPEND)
		c->flag |= CAPPEND;

	cclose(c);
	poperror();
	return 0;
}
