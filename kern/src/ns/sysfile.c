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

enum {
	DIRSIZE = STATFIXLEN + 32 * 4,
	DIRREADLIM = 2048,	/* should handle the largest reasonable directory entry */
	DIRREADSIZE=8192,	/* Just read a lot. Memory is cheap, lots of bandwidth,
				 * and RPCs are very expensive. At the same time,
				 * let's not yet exceed a common MSIZE. */
};

int newfd(struct chan *c, int low_fd, int oflags, bool must_use_low)
{
	int ret = insert_obj_fdt(&current->open_files, c, low_fd,
	                         oflags & O_CLOEXEC ? FD_CLOEXEC : 0,
	                         must_use_low, FALSE);
	if (ret >= 0)
		cclose(c);
	return ret;
}

struct chan *fdtochan(struct fd_table *fdt, int fd, int mode, int chkmnt,
                      int iref)
{
	struct chan *c;

	c = lookup_fd(fdt, fd, iref, FALSE);
	if (!c) {
		/* We lost the info about why there was a problem (we used to track file
		 * group closed too, can add that in later). */
		error(EBADF, ERROR_FIXME);
	}
	if (chkmnt && (c->flag & CMSG)) {
		if (iref)
			cclose(c);
		error(EBADF, ERROR_FIXME);
	}
	if (mode < 0)
		return c;
	if ((mode & c->mode) != mode) {
		if (iref)
			cclose(c);
		error(EBADF,
		      "FD access mode failure: chan mode 0x%x, wanted 0x%x (opened with 0 instead of O_READ?)",
		      c->mode, mode);
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

	if (mode == O_READ)
		r = devtab[c->type].read(c, buf, n, c->offset);
	else if (mode == O_WRITE)
		r = devtab[c->type].write(c, buf, n, c->offset);
	else
		error(ENOSYS, "kchanio: use only O_READ xor O_WRITE");

	spin_lock(&c->lock);
	c->offset += r;
	spin_unlock(&c->lock);
	poperror();
	return r;
}

int openmode(uint32_t omode)
{
/* GIANT WARNING: if this ever throws, ipopen (and probably many others) will
 * screw up refcnts of Qctl, err, data, etc */
#if 0
	/* this is the old plan9 style.  i think they want to turn exec into read,
	 * and strip off anything higher, and just return the RD/WR style bits.  not
	 * stuff like ORCLOSE.  the lack of OEXCL might be a bug on their part (it's
	 * the only one of their non-RW-related flags that isn't masked out).
	 *
	 * Note that we no longer convert OEXEC/O_EXEC to O_READ, and instead return
	 * just the O_ACCMODE bits. */
	if (o >= (OTRUNC | OCEXEC | ORCLOSE | OEXEC))
		error(EINVAL, ERROR_FIXME);
	o &= ~(OTRUNC | OCEXEC | ORCLOSE);
	if (o > OEXEC)
		error(EINVAL, ERROR_FIXME);
	if (o == OEXEC)
		return OREAD;
	return o;
#endif
	/* no error checking (we have a shitload of flags anyway), and we return the
	 * basic access modes (RD/WR/ETC) */
	return omode & O_ACCMODE;
}

void fdclose(struct fd_table *fdt, int fd)
{
	close_fd(fdt, fd);
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

int sysclose(int fd)
{
	ERRSTACK(1);
	struct fd_table *fdt = &current->open_files;

	if (waserror()) {
		poperror();
		return -1;
	}
	/*
	 * Take no reference on the chan because we don't really need the
	 * data structure, and are calling fdtochan only for error checks.
	 * fdclose takes care of processes racing through here.
	 */
	fdtochan(fdt, fd, -1, 0, 0);
	fdclose(fdt, fd);
	poperror();
	return 0;
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

	openmode(mode & ~O_EXCL);	/* error check only; OEXCL okay here */
	c = namec(path, Acreate, mode, perm);
	if (waserror()) {
		cclose(c);
		nexterror();
	}
	/* 9ns mode is the O_FLAGS and perm is glibc mode */
	fd = newfd(c, 0, mode, FALSE);
	if (fd < 0)
		error(-fd, ERROR_FIXME);
	poperror();

	poperror();
	return fd;
}

int sysdup(int old, int low_fd, bool must_use_low)
{
	ERRSTACK(1);
	int fd;
	struct chan *c;

	if (waserror()) {
		poperror();
		return -1;
	}
	c = fdtochan(&current->open_files, old, -1, 0, 1);
	if (c->qid.type & QTAUTH) {
		cclose(c);
		error(EPERM, ERROR_FIXME);
	}
	fd = newfd(c, low_fd, 0, must_use_low);
	if (fd < 0) {
		cclose(c);
		error(-fd, ERROR_FIXME);
	}
	poperror();
	return fd;
}

/* Could pass in the fdt instead of the proc, but we used to need the to_proc
 * for now so we can claim a VFS FD.  Careful, we don't close the old chan. */
int sys_dup_to(struct proc *from_proc, unsigned int from_fd,
               struct proc *to_proc, unsigned int to_fd)
{
	ERRSTACK(1);
	int ret;
	struct chan *c;

	if (waserror()) {
		poperror();
		return -1;
	}
	c = fdtochan(&from_proc->open_files, from_fd, -1, 0, 1);
	if (c->qid.type & QTAUTH) {
		cclose(c);
		error(EPERM, ERROR_FIXME);
	}
	ret = insert_obj_fdt(&to_proc->open_files, c, to_fd, 0, TRUE, FALSE);
	/* drop the ref from fdtochan.  if insert succeeded, there is one other ref
	 * stored in the FDT */
	cclose(c);
	if (ret < 0)
		error(EFAIL, "Can't insert FD %d into FDG", to_fd);
	poperror();
	return 0;
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
	c = fdtochan(&current->open_files, fd, -1, 0, 1);
	s = NULL;
	if (c->name != NULL) {
		s = kzmalloc(c->name->len + 1, 0);
		if (s == NULL) {
			cclose(c);
			error(ENOMEM, ERROR_FIXME);
		}
		memmove(s, c->name->s, c->name->len + 1);
	}
	cclose(c);
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
	c = fdtochan(&current->open_files, fd, O_RDWR, 0, 1);
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

	fd = newfd(ac, 0, 0, FALSE);
	if (fd < 0)
		error(-fd, ERROR_FIXME);
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
		error(EINVAL, ERROR_FIXME);

	c = fdtochan(&current->open_files, fd, O_RDWR, 0, 1);
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

int sysfwstat(int fd, uint8_t * buf, int n)
{
	ERRSTACK(2);
	struct chan *c;

	if (waserror()) {
		poperror();
		return -1;
	}

	validstat(buf, n, 0);
	c = fdtochan(&current->open_files, fd, -1, 1, 1);
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
		error(EINVAL, ERROR_FIXME);

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
	bc.c = fdtochan(&current->open_files, fd, O_RDWR, 0, 1);
	if (afd >= 0)
		ac.c = fdtochan(&current->open_files, afd, O_RDWR, 0, 1);
	mntparam.chan = bc.c;
	mntparam.authchan = ac.c;
	mntparam.spec = spec;
	mntparam.flags = flags;
	c0.c = devtab[devno("mnt", 0)].attach((char *)&mntparam);

	r = bindmount(c0.c, old, flags, spec);
	poperror();
	cclose(ac.c);
	cclose(bc.c);
	cclose(c0.c);

	return r;
}

int sysunmount(char *src_path, char *onto_path)
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

	cmount.c = namec(onto_path, Amount, 0, 0);
	if (src_path != NULL && src_path[0] != '\0') {
		/*
		 * This has to be namec(..., Aopen, ...) because
		 * if arg[0] is something like /srv/cs or /fd/0,
		 * opening it is the only way to get at the real
		 * Chan underneath.
		 */
		cmounted.c = namec(src_path, Aopen, O_READ, 0);
	}

	cunmount(cmount.c, cmounted.c);
	poperror();
	cclose(cmount.c);
	cclose(cmounted.c);
	return 0;
}

int sysopenat(int fromfd, char *path, int vfs_flags)
{
	ERRSTACK(1);
	int fd;
	struct chan *c = 0, *from = 0;

	if (waserror()) {
		cclose(c);
		poperror();
		return -1;
	}
	openmode(vfs_flags);	/* error check only */
	if ((path[0] == '/') || (fromfd == AT_FDCWD)) {
		c = namec(path, Aopen, vfs_flags, 0);
	} else {
		/* We don't cclose from.  namec_from will convert it to the new chan
		 * during the walk process (c).  It'll probably close from internally,
		 * and give us something new for c.  On error, namec_from will cclose
		 * from. */
		from = fdtochan(&current->open_files, fromfd, -1, FALSE, TRUE);
		if (!(from->flag & O_PATH))
			error(EINVAL, "Cannot openat from a non-O_PATH FD");
		c = namec_from(from, path, Aopen, vfs_flags, 0);
	}
	fd = newfd(c, 0, vfs_flags, FALSE);
	if (fd < 0)
		error(-fd, ERROR_FIXME);
	poperror();
	return fd;
}

int sysopen(char *path, int vfs_flags)
{
	return sysopenat(AT_FDCWD, path, vfs_flags);
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
			 * hack, we need to repeat unionread if we saw a ENODATA */
			if (waserror()) {
				if (get_errno() == ENODATA) {
					runlock(&m->lock);
					qunlock(&c->umqlock);
					nexterror();
				}
				/* poperror done below for either branch */
			} else {
				if (c->umc == NULL) {
					c->umc = cclone(mount->to);
					c->umc = devtab[c->umc->type].open(c->umc,
									   O_READ);
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
	void *real_va = va;

	if (waserror()) {
		poperror();
		return -1;
	}

	c = fdtochan(&current->open_files, fd, O_READ, 1, 1);
	if (waserror()) {
		cclose(c);
		nexterror();
	}

	if (n < 0)
		error(EINVAL, ERROR_FIXME);

	dir = c->qid.type & QTDIR;

	/* kdirent hack: userspace is expecting kdirents, but all of 9ns
	 * produces Ms.  Just save up what we don't use and append the
	 * new stuff later. Allocate DIRREADSIZE bytes for that purpose.
	 */
	if (dir) {
		int amt;
		/* expecting only one dirent at a time, o/w we're busted */
		assert(n >= sizeof(struct kdirent));
		if (!c->buf) {
			c->buf = kmalloc(DIRREADSIZE, MEM_WAIT);
			c->bufused = 0;
		}
		/* Attempt to extract an M, in case there was some already */
		amt = convM2kdirent(c->buf, c->bufused, real_va, 0);
		if (amt) {
			c->bufused -= amt;
			memmove(c->buf, c->buf + amt, c->bufused);
			n = sizeof(struct kdirent);
			goto out;
		}
		/* debugging */
		if (waserror()) {
			printk("Well, sysread of a dir sucks.%s \n", current_errstr());
			nexterror();
		}
		va = c->buf + c->bufused;
		n = DIRREADSIZE - c->bufused;
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
			error(EINVAL, ERROR_FIXME);
		if (off == 0) {
			if (offp == NULL) {
				spin_lock(&c->lock);
				c->offset = 0;
				c->dri = 0;
				spin_unlock(&c->lock);
			}
			unionrewind(c);
		}
		if (! c->ateof) {
			n = devtab[c->type].read(c, va, n, off);
			if (n == 0 && dir)
				c->ateof = 1;
		} else {
			n = 0;
		}
		spin_lock(&c->lock);
		c->offset += n;
		spin_unlock(&c->lock);
	}

	/* dirty kdirent hack */
	if (dir) {
		int amt;
		c->bufused = c->bufused + n;
		/* extract an M from the front, then shift the remainder back */
		amt = convM2kdirent(c->buf, c->bufused, real_va, 0);
		c->bufused -= amt;
		memmove(c->buf, c->buf + amt, c->bufused);
		n = amt ? sizeof(struct kdirent) : 0;
		poperror();	/* matching our debugging waserror */
	}

out:
	poperror();
	cclose(c);

	poperror();
	return n;
}

/* Reads exactly n bytes from chan c, starting at its offset.  Can block, but if
 * we get 0 back too soon (EOF or error), then we'll error out with ENODATA.
 * That might need a little work - if there was a previous error, then we
 * clobbered it and only know ENODATA but not why we completed early. */
void read_exactly_n(struct chan *c, void *vp, long n)
{
	char *p;
	long nn;
	int total = 0, want = n;

	p = vp;
	while (n > 0) {
		nn = devtab[c->type].read(c, p, n, c->offset);
		printd("readn: Got %d@%lld\n", nn, c->offset);
		if (nn == 0)
			error(ENODATA, "wanted %d, got %d", want, total);
		spin_lock(&c->lock);
		c->offset += nn;
		spin_unlock(&c->lock);
		p += nn;
		n -= nn;
		total += nn;
	}
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

	c = fdtochan(&current->open_files, fd, -1, 1, 1);
	if (waserror()) {
		cclose(c);
		nexterror();
	}
	switch (whence) {
		case 0:
			if (c->qid.type & QTDIR) {
				if (off != 0)
					error(EISDIR, ERROR_FIXME);
				unionrewind(c);
			} else if (off < 0)
				error(EINVAL, ERROR_FIXME);
			spin_lock(&c->lock);	/* lock for int64_t assignment */
			c->offset = off;
			spin_unlock(&c->lock);
			break;

		case 1:
			if (c->qid.type & QTDIR)
				error(EISDIR, ERROR_FIXME);
			spin_lock(&c->lock);	/* lock for read/write update */
			off += c->offset;
			if (off < 0) {
				spin_unlock(&c->lock);
				error(EINVAL, ERROR_FIXME);
			}
			c->offset = off;
			spin_unlock(&c->lock);
			break;

		case 2:
			if (c->qid.type & QTDIR)
				error(EISDIR, ERROR_FIXME);
			dir = chandirstat(c);
			if (dir == NULL)
				error(EFAIL, "internal error: stat error in seek");
			off += dir->length;
			kfree(dir);
			if (off < 0)
				error(EINVAL, ERROR_FIXME);
			spin_lock(&c->lock);	/* lock for read/write update */
			c->offset = off;
			spin_unlock(&c->lock);
			break;

		default:
			error(EINVAL, ERROR_FIXME);
			break;
	}
	poperror();
	c->dri = 0;
	cclose(c);
	poperror();
	return off;
}

void validstat(uint8_t * s, int n, int slashok)
{

	int m;
	char buf[64];

	if (statcheck(s, n) < 0)
		error(EINVAL, ERROR_FIXME);
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
	if (m + 1 > sizeof buf) {
		return;
	}
	memmove(buf, s, m);
	buf[m] = '\0';
	/* name could be '/' */
	if (strcmp(buf, "/") != 0)
		validname(buf, slashok);
}

int sysfstat(int fd, uint8_t *buf, int n)
{
	ERRSTACK(2);
	struct chan *c;

	if (waserror()) {
		poperror();
		return -1;
	}

	c = fdtochan(&current->open_files, fd, -1, 0, 1);
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
	buf = kmalloc(n, MEM_WAIT);
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
	buf = kmalloc(n, MEM_WAIT);
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
	c = fdtochan(&current->open_files, fd, O_WRITE, 1, 1);
	if (waserror()) {
		cclose(c);
		nexterror();
	}
	if (c->qid.type & QTDIR)
		error(EISDIR, ERROR_FIXME);

	if (n < 0)
		error(EINVAL, ERROR_FIXME);

	if (offp == NULL) {
		/* append changes the offset to the end, and even if we fail later, this
		 * change will persist */
		if (c->flag & O_APPEND) {
			dir = chandirstat(c);
			if (!dir)
				error(EFAIL, "internal error: stat error in append write");
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
		error(EINVAL, ERROR_FIXME);
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
	return m;
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

	validstat(buf, n, 0);
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

	c = fdtochan(&current->open_files, fd, -1, 0, 1);
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
	if (ts <= 0) {
		return ts;
	}

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
		error(EFAIL, "bad directory format");

	*d = kzmalloc(n * sizeof(**d) + ss, 0);
	if (*d == NULL)
		error(ENOMEM, ERROR_FIXME);

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
			error(EFAIL, "bad directory entry");
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
		error(ENOMEM, ERROR_FIXME);
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

	c = fdtochan(&current->open_files, fd, -1, 0, 1);
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

void print_chaninfo(struct chan *c)
{

	char buf[128] = { 0 };
	bool has_dev = c->type != -1;
	bool has_chaninfo = has_dev && devtab[c->type].chaninfo;

	printk("Chan flags: %p, pathname: %s, ref: %d, Dev: %s, Devinfo: %s",
		   c->flag,
		   c->name ? c->name->s : "no cname",
		   kref_refcnt(&c->ref),
		   has_dev ? devtab[c->type].name : "no dev",
		   has_chaninfo ? devtab[c->type].chaninfo(c, buf, sizeof(buf)) : "");
	if (!has_chaninfo)
		printk("qid.path: %p\n", c->qid.path);
	printk("\n");
}

/* TODO: 9ns ns inheritance flags: Shared, copied, or empty.  The old fgrp is
 * managed by the fd_table, which is handled outside this function.  We share
 * the pgrp. */
int plan9setup(struct proc *new_proc, struct proc *parent, int flags)
{

	struct kref *new_dot_ref;
	ERRSTACK(1);
	if (waserror()) {
		printk("plan9setup failed, %s\n", current_errstr());
		poperror();
		return -1;
	}
	if (!parent) {
		/* We are probably spawned by the kernel directly, and have no parent to
		 * inherit from. */
		new_proc->pgrp = newpgrp();
		new_proc->slash = namec("#root", Atodir, 0, 0);
		if (!new_proc->slash)
			panic("no root device");
		/* Want the name to be "/" instead of "#root" */
		cnameclose(new_proc->slash->name);
		new_proc->slash->name = newcname("/");
		new_proc->dot = cclone(new_proc->slash);
		poperror();
		return 0;
	}
	/* Shared semantics */
	kref_get(&parent->pgrp->ref, 1);
	new_proc->pgrp = parent->pgrp;
	/* copy semantics on / and . (doesn't make a lot of sense in akaros o/w) */
	/* / should never disappear while we hold a ref to parent */
	chan_incref(parent->slash);
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
	c = fdtochan(&current->open_files, fd, -1, 0, 1);

	ret = c->mode;
	ret |= c->flag & CEXTERNAL_FLAGS;

	cclose(c);
	poperror();
	return ret;
}

static bool cexternal_flags_differ(int set1, int set2, int flags)
{
	flags &= CEXTERNAL_FLAGS;
	return (set1 & flags) ^ (set2 & flags);
}

int fd_setfl(int fd, int flags)
{
	ERRSTACK(2);
	struct chan *c;
	int ret = 0;

	if (waserror()) {
		poperror();
		return -1;
	}
	c = fdtochan(&current->open_files, fd, -1, 0, 1);
	if (waserror()) {
		cclose(c);
		nexterror();
	}
	if (cexternal_flags_differ(flags, c->flag, O_CLOEXEC)) {
		/* TODO: The whole CCEXEC / O_CLOEXEC on 9ns needs work */
		error(EINVAL, "can't toggle O_CLOEXEC with setfl");
	}
	if (cexternal_flags_differ(flags, c->flag, O_REMCLO))
		error(EINVAL, "can't toggle O_REMCLO with setfl");
	if (cexternal_flags_differ(flags, c->flag, O_PATH))
		error(EINVAL, "can't toggle O_PATH with setfl");
	/* Devices can do various prep work, including RPCs to other servers (#mnt)
	 * for a chan_ctl operation.  If they want to not support the new flags,
	 * they can throw an error. */
	if (devtab[c->type].chan_ctl)
		ret = devtab[c->type].chan_ctl(c, flags & CEXTERNAL_FLAGS);
	c->flag = (c->flag & ~CEXTERNAL_FLAGS) | (flags & CEXTERNAL_FLAGS);
	poperror();
	cclose(c);
	poperror();
	return ret;
}
