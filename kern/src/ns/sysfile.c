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
#include <net/ip.h>
#include <rcu.h>

/* TODO: these sizes are hokey.  DIRSIZE is used in chandirstat, and it looks
 * like it's the size of a common-case stat. */
enum {
	DIRSIZE = STAT_FIX_LEN_AK + 32 * STAT_NR_STRINGS_AK,
	DIRREADLIM = 2048,	/* should handle the largest reasonable directory entry */
	DIRREADSIZE=8192,	/* Just read a lot. Memory is cheap, lots of bandwidth,
				 * and RPCs are very expensive. At the same time,
				 * let's not yet exceed a common MSIZE. */
};

int newfd(struct chan *c, int low_fd, int oflags, bool must_use_low)
{
	int ret = insert_obj_fdt(&current->open_files, c, low_fd,
	                         oflags & O_CLOEXEC ? FD_CLOEXEC : 0,
	                         must_use_low);
	if (ret >= 0)
		cclose(c);
	return ret;
}

struct chan *fdtochan(struct fd_table *fdt, int fd, int mode, int chkmnt,
                      int iref)
{
	struct chan *c;

	c = lookup_fd(fdt, fd, iref);
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

static void set_dot(struct chan *c)
{
	c = atomic_swap_ptr((void**)&current->dot, c);
	synchronize_rcu();
	cclose(c);
}

int syschdir(char *path)
{
	ERRSTACK(1);
	struct chan *c;

	if (waserror()) {
		poperror();
		return -1;
	}
	c = namec(path, Atodir, 0, 0, NULL);
	poperror();
	set_dot(c);
	return 0;
}

int sysfchdir(int fd)
{
	ERRSTACK(1);
	struct chan *c;

	if (waserror()) {
		poperror();
		return -1;
	}
	c = fdtochan(&current->open_files, fd, -1, 0, 1);
	poperror();

	/* This is a little hokey.  Ideally, we'd only allow O_PATH fds to be
	 * fchdir'd.  Linux/POSIX lets you do arbitrary FDs.  Luckily, we stored the
	 * name when we walked (__namec_from), so we should be able to recreate the
	 * chan.  Using namec() with channame() is a more heavy-weight cclone(), but
	 * also might have issues if the chan has since been removed or the
	 * namespace is otherwise different from when the original fd/chan was first
	 * created. */
	if (c->flag & O_PATH) {
		set_dot(c);
		return 0;
	}
	if (waserror()) {
		cclose(c);
		poperror();
		return -1;
	}
	syschdir(channame(c));
	cclose(c);
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
	c = namec(path, Acreate, mode, perm, NULL);
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
	ret = insert_obj_fdt(&to_proc->open_files, c, to_fd, 0, TRUE);
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

char *sysgetcwd(void)
{
	char *s = NULL;
	struct chan *dot;

	rcu_read_lock();
	dot = rcu_dereference(current->dot);
	kref_get(&dot->ref, 1);
	rcu_read_unlock();
	if (dot->name)
		kstrdup(&s, dot->name->s);
	cclose(dot);
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

	c1 = namec(old, Amount, 0, 0, NULL);
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

	c0 = namec(new, Abind, 0, 0, NULL);
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

int syssymlink(char *new_path, char *old_path)
{
	ERRSTACK(1);
	struct chan *c;

	if (waserror()) {
		poperror();
		return -1;
	}
	validname(old_path, true);
	c = namec(new_path, Acreate, O_EXCL,
	          DMSYMLINK | S_IRWXU | S_IRWXG | S_IRWXO, old_path);
	cclose(c);
	poperror();
	return 0;
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
	c0.c = devtab[devno("mnt", 0)].attach((char *)&mntparam);
	if (flags & MCACHE)
		c0.c = devtab[devno("gtfs", 0)].attach((char*)c0.c);
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

	cmount.c = namec(onto_path, Amount, 0, 0, NULL);
	if (src_path != NULL && src_path[0] != '\0') {
		/*
		 * This has to be namec(..., Aopen, ...) because
		 * if arg[0] is something like /srv/cs or /fd/0,
		 * opening it is the only way to get at the real
		 * Chan underneath.
		 */
		cmounted.c = namec(src_path, Aopen, O_READ, 0, NULL);
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
		c = namec(path, Aopen, vfs_flags, 0, NULL);
	} else {
		/* We don't cclose from.  namec_from will convert it to the new chan
		 * during the walk process (c).  It'll probably close from internally,
		 * and give us something new for c.  On error, namec_from will cclose
		 * from. */
		from = fdtochan(&current->open_files, fromfd, -1, FALSE, TRUE);
		if (!(from->flag & O_PATH))
			error(EINVAL, "Cannot openat from a non-O_PATH FD");
		c = namec_from(from, path, Aopen, vfs_flags, 0, NULL);
	}
	/* Devices should catch this, but just in case, we'll catch it. */
	if ((c->qid.type & QTSYMLINK) && (vfs_flags & O_NOFOLLOW))
		error(ELOOP, "no-follow open of a symlink");
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

	c = namec(path, Aremove, 0, 0, NULL);
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

int sysrename(char *from_path, char *to_path)
{
	ERRSTACK(1);
	struct chan *volatile renamee = NULL;
	struct chan *parent_chan;

	if (waserror()) {
		cclose(renamee);
		poperror();
		return -1;
	}
	renamee = namec(from_path, Aremove, 0, 0, NULL);
	/* We might need to support wstat for 'short' rename (intra-directory, with
	 * no slashes).  Til then, we can just go with EXDEV. */
	if (!devtab[renamee->type].rename)
		error(EXDEV, "device does not support rename");
	parent_chan = namec(to_path, Arename, 0, 0, (char*)renamee);
	/* When we're done, renamee still points to the file, but it's in the new
	 * location.  Its cname is still the old location, similar to remove.  If
	 * anyone cares, we can change it.  parent_chan still points to the parent -
	 * it didn't get moved like create does.  Though it does have the name of
	 * the new location.  If we want, we can hand that to renamee.  It's a moot
	 * point, since they are both getting closed. */
	cclose(renamee);
	cclose(parent_chan);
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
	s += STAT_FIX_LEN_9P - STAT_NR_STRINGS_9P * BIT16SZ;
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

static int __stat(char *path, uint8_t *buf, int n, int flags)
{
	ERRSTACK(2);
	struct chan *c;

	if (waserror()) {
		poperror();
		return -1;
	}

	c = namec(path, Aaccess, flags, 0, NULL);
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

int sysstat(char *path, uint8_t *buf, int n)
{
	return __stat(path, buf, n, 0);
}

int syslstat(char *path, uint8_t *buf, int n)
{
	return __stat(path, buf, n, O_NOFOLLOW);
}

int sysstatakaros(char *path, struct kstat *ks, int flags)
{

	int n = 4096;
	uint8_t *buf;
	buf = kmalloc(n, MEM_WAIT);
	n = __stat(path, buf, n, flags);
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
	c = namec(path, Aaccess, 0, 0, NULL);
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
		d = kzmalloc(sizeof(struct dir) + nd, MEM_WAIT);
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

static struct dir *__dir_stat(char *name, int flags)
{
	ERRSTACK(2);
	struct chan *c;
	struct dir *d;

	if (waserror()) {
		poperror();
		return NULL;
	}

	c = namec(name, Aaccess, flags, 0, NULL);
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

struct dir *sysdirstat(char *name)
{
	return __dir_stat(name, 0);
}

struct dir *sysdirlstat(char *name)
{
	return __dir_stat(name, O_NOFOLLOW);
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
	buf = kzmalloc(r, MEM_WAIT);
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
	buf = kzmalloc(r, MEM_WAIT);
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
		/* Note 's' is ignored by convM2kdirent */
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

	print_lock();
	printk("Chan flags: %p, pathname: %s, ref: %d, Dev: %s, Devinfo: %s",
		   c->flag,
		   c->name ? c->name->s : "no cname",
		   kref_refcnt(&c->ref),
		   has_dev ? devtab[c->type].name : "no dev",
		   has_chaninfo ? devtab[c->type].chaninfo(c, buf, sizeof(buf)) : "");
	if (!has_chaninfo)
		printk("qid.path: %p\n", c->qid.path);
	printk("\n");
	print_unlock();
}

/* TODO: 9ns ns inheritance flags: Shared, copied, or empty.  The old fgrp is
 * managed by the fd_table, which is handled outside this function.  We share
 * the pgrp. */
int plan9setup(struct proc *new_proc, struct proc *parent, int flags)
{

	struct chan *new_dot;

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
		new_proc->slash = namec("#kfs", Atodir, 0, 0, NULL);
		if (!new_proc->slash)
			panic("no kfs device");
		/* Want the name to be "/" instead of "#kfs" */
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

	rcu_read_lock();
	new_dot = rcu_dereference(parent->dot);
	kref_get(&new_dot->ref, 1);
	rcu_read_unlock();
	new_proc->dot = new_dot;

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
	if (!devtab[c->type].chan_ctl)
		error(EINVAL, "can't setfl, %s has no chan_ctl", chan_dev_name(c));
	ret = devtab[c->type].chan_ctl(c, CCTL_SET_FL, flags & CEXTERNAL_FLAGS,
	                               0, 0, 0);
	c->flag = (c->flag & ~CEXTERNAL_FLAGS) | (flags & CEXTERNAL_FLAGS);
	poperror();
	cclose(c);
	poperror();
	return ret;
}

int fd_sync(int fd)
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
	if (!devtab[c->type].chan_ctl)
		error(EINVAL, "can't fsync, %s has no chan_ctl", chan_dev_name(c));
	ret = devtab[c->type].chan_ctl(c, CCTL_SYNC, 0, 0, 0, 0);
	poperror();
	cclose(c);
	poperror();
	return ret;
}

ssize_t kread_file(struct file_or_chan *file, void *buf, size_t sz)
{
	/* TODO: (KFOP) (VFS kernel read/writes need to be from a ktask) */
	uintptr_t old_ret = switch_to_ktask();
	off64_t dummy = 0;
	ssize_t cpy_amt = foc_read(file, buf, sz, dummy);

	switch_back_from_ktask(old_ret);
	return cpy_amt;
}

/* Reads the contents of an entire file into a buffer, returning that buffer.
 * On error, prints something useful and returns 0 */
void *kread_whole_file(struct file_or_chan *file)
{
	size_t size;
	void *contents;
	ssize_t cpy_amt;

	size = foc_get_len(file);
	contents = kmalloc(size, MEM_WAIT);
	cpy_amt = kread_file(file, contents, size);
	if (cpy_amt < 0) {
		printk("Error %d reading file %s\n", get_errno(), foc_to_name(file));
		kfree(contents);
		return 0;
	}
	if (cpy_amt != size) {
		printk("Read %d, needed %d for file %s\n", cpy_amt, size,
		       foc_to_name(file));
		kfree(contents);
		return 0;
	}
	return contents;
}

/* Process-related File management functions */

/* Given any FD, get the appropriate object, 0 o/w. Set incref if you want a
 * reference count (which is a 9ns thing, you can't use the pointer if you
 * didn't incref). */
void *lookup_fd(struct fd_table *fdt, int fd, bool incref)
{
	void *retval = 0;

	if (fd < 0)
		return 0;
	spin_lock(&fdt->lock);
	if (fdt->closed) {
		spin_unlock(&fdt->lock);
		return 0;
	}
	if (fd < fdt->max_fdset) {
		if (GET_BITMASK_BIT(fdt->open_fds->fds_bits, fd)) {
			/* while max_files and max_fdset might not line up, we should never
			 * have a valid fdset higher than files */
			assert(fd < fdt->max_files);
			retval = fdt->fd[fd].fd_chan;
			if (incref)
				chan_incref((struct chan*)retval);
		}
	}
	spin_unlock(&fdt->lock);
	return retval;
}

/* Grow the vfs fd set */
static int grow_fd_set(struct fd_table *open_files)
{
	int n;
	struct file_desc *nfd, *ofd;

	/* Only update open_fds once. If currently pointing to open_fds_init, then
	 * update it to point to a newly allocated fd_set with space for
	 * NR_FILE_DESC_MAX */
	if (open_files->open_fds == (struct fd_set*)&open_files->open_fds_init) {
		open_files->open_fds = kzmalloc(sizeof(struct fd_set), 0);
		memmove(open_files->open_fds, &open_files->open_fds_init,
		        sizeof(struct small_fd_set));
	}

	/* Grow the open_files->fd array in increments of NR_OPEN_FILES_DEFAULT */
	n = open_files->max_files + NR_OPEN_FILES_DEFAULT;
	if (n > NR_FILE_DESC_MAX)
		return -EMFILE;
	nfd = kzmalloc(n * sizeof(struct file_desc), 0);
	if (nfd == NULL)
		return -ENOMEM;

	/* Move the old array on top of the new one */
	ofd = open_files->fd;
	memmove(nfd, ofd, open_files->max_files * sizeof(struct file_desc));

	/* Update the array and the maxes for both max_files and max_fdset */
	open_files->fd = nfd;
	open_files->max_files = n;
	open_files->max_fdset = n;

	/* Only free the old one if it wasn't pointing to open_files->fd_array */
	if (ofd != open_files->fd_array)
		kfree(ofd);
	return 0;
}

/* Free the vfs fd set if necessary */
static void free_fd_set(struct fd_table *open_files)
{
	void *free_me;

	if (open_files->open_fds != (struct fd_set*)&open_files->open_fds_init) {
		assert(open_files->fd != open_files->fd_array);
		/* need to reset the pointers to the internal addrs, in case we take a
		 * look while debugging.  0 them out, since they have old data.  our
		 * current versions should all be closed. */
		memset(&open_files->open_fds_init, 0, sizeof(struct small_fd_set));
		memset(&open_files->fd_array, 0, sizeof(open_files->fd_array));

		free_me = open_files->open_fds;
		open_files->open_fds = (struct fd_set*)&open_files->open_fds_init;
		kfree(free_me);

		free_me = open_files->fd;
		open_files->fd = open_files->fd_array;
		kfree(free_me);
	}
}

/* If FD is in the group, remove it, decref it, and return TRUE. */
bool close_fd(struct fd_table *fdt, int fd)
{
	struct chan *chan = 0;
	struct fd_tap *tap = 0;
	bool ret = FALSE;

	if (fd < 0)
		return FALSE;
	spin_lock(&fdt->lock);
	if (fd < fdt->max_fdset) {
		if (GET_BITMASK_BIT(fdt->open_fds->fds_bits, fd)) {
			/* while max_files and max_fdset might not line up, we should never
			 * have a valid fdset higher than files */
			assert(fd < fdt->max_files);
			chan = fdt->fd[fd].fd_chan;
			tap = fdt->fd[fd].fd_tap;
			fdt->fd[fd].fd_chan = 0;
			fdt->fd[fd].fd_tap = 0;
			CLR_BITMASK_BIT(fdt->open_fds->fds_bits, fd);
			if (fd < fdt->hint_min_fd)
				fdt->hint_min_fd = fd;
			ret = TRUE;
		}
	}
	spin_unlock(&fdt->lock);
	/* Need to decref/cclose outside of the lock; they could sleep */
	cclose(chan);
	if (tap)
		kref_put(&tap->kref);
	return ret;
}

static int __get_fd(struct fd_table *open_files, int low_fd, bool must_use_low)
{
	int slot = -1;
	int error;
	bool update_hint = TRUE;

	if ((low_fd < 0) || (low_fd > NR_FILE_DESC_MAX))
		return -EINVAL;
	if (open_files->closed)
		return -EINVAL;	/* won't matter, they are dying */
	if (must_use_low && GET_BITMASK_BIT(open_files->open_fds->fds_bits, low_fd))
		return -ENFILE;
	if (low_fd > open_files->hint_min_fd)
		update_hint = FALSE;
	else
		low_fd = open_files->hint_min_fd;
	/* Loop until we have a valid slot (we grow the fd_array at the bottom of
	 * the loop if we haven't found a slot in the current array */
	while (slot == -1) {
		for (low_fd; low_fd < open_files->max_fdset; low_fd++) {
			if (GET_BITMASK_BIT(open_files->open_fds->fds_bits, low_fd))
				continue;
			slot = low_fd;
			SET_BITMASK_BIT(open_files->open_fds->fds_bits, slot);
			assert(slot < open_files->max_files &&
			       open_files->fd[slot].fd_chan == 0);
			/* We know slot >= hint, since we started with the hint */
			if (update_hint)
				open_files->hint_min_fd = slot + 1;
			break;
		}
		if (slot == -1)	{
			if ((error = grow_fd_set(open_files)))
				return error;
		}
	}
	return slot;
}

/* Insert a file or chan (obj, chosen by vfs) into the fd group with fd_flags.
 * If must_use_low, then we have to insert at FD = low_fd.  o/w we start looking
 * for empty slots at low_fd. */
int insert_obj_fdt(struct fd_table *fdt, void *obj, int low_fd, int fd_flags,
                   bool must_use_low)
{
	int slot;

	spin_lock(&fdt->lock);
	slot = __get_fd(fdt, low_fd, must_use_low);
	if (slot < 0) {
		spin_unlock(&fdt->lock);
		return slot;
	}
	assert(slot < fdt->max_files &&
	       fdt->fd[slot].fd_chan == 0);
	chan_incref((struct chan*)obj);
	fdt->fd[slot].fd_chan = obj;
	fdt->fd[slot].fd_flags = fd_flags;
	spin_unlock(&fdt->lock);
	return slot;
}

/* Closes all open files.  Mostly just a "put" for all files.  If cloexec, it
 * will only close the FDs with FD_CLOEXEC (opened with O_CLOEXEC or fcntld).
 *
 * Notes on concurrency:
 * - Can't hold spinlocks while we call cclose, since it might sleep eventually.
 * - We're called from proc_destroy, so we could have concurrent openers trying
 *   to add to the group (other syscalls), hence the "closed" flag.
 * - dot and slash chans are dealt with in proc_free.  its difficult to close
 *   and zero those with concurrent syscalls, since those are a source of krefs.
 * - Once we lock and set closed, no further additions can happen.  To simplify
 *   our closes, we also allow multiple calls to this func (though that should
 *   never happen with the current code). */
void close_fdt(struct fd_table *fdt, bool cloexec)
{
	struct chan *chan;
	struct file_desc *to_close;
	int idx = 0;

	to_close = kzmalloc(sizeof(struct file_desc) * fdt->max_files,
	                    MEM_WAIT);
	spin_lock(&fdt->lock);
	if (fdt->closed) {
		spin_unlock(&fdt->lock);
		kfree(to_close);
		return;
	}
	for (int i = 0; i < fdt->max_fdset; i++) {
		if (GET_BITMASK_BIT(fdt->open_fds->fds_bits, i)) {
			/* while max_files and max_fdset might not line up, we should never
			 * have a valid fdset higher than files */
			assert(i < fdt->max_files);
			if (cloexec && !(fdt->fd[i].fd_flags & FD_CLOEXEC))
				continue;
			chan = fdt->fd[i].fd_chan;
			to_close[idx].fd_tap = fdt->fd[i].fd_tap;
			fdt->fd[i].fd_tap = 0;
			fdt->fd[i].fd_chan = 0;
			to_close[idx++].fd_chan = chan;
			CLR_BITMASK_BIT(fdt->open_fds->fds_bits, i);
		}
	}
	/* it's just a hint, we can build back up from being 0 */
	fdt->hint_min_fd = 0;
	if (!cloexec) {
		free_fd_set(fdt);
		fdt->closed = TRUE;
	}
	spin_unlock(&fdt->lock);
	/* We go through some hoops to close/decref outside the lock.  Nice for not
	 * holding the lock for a while; critical in case the decref/cclose sleeps
	 * (it can) */
	for (int i = 0; i < idx; i++) {
		cclose(to_close[i].fd_chan);
		if (to_close[i].fd_tap)
			kref_put(&to_close[i].fd_tap->kref);
	}
	kfree(to_close);
}

/* Inserts all of the files from src into dst, used by sys_fork(). */
void clone_fdt(struct fd_table *src, struct fd_table *dst)
{
	struct chan *chan;
	int ret;

	spin_lock(&src->lock);
	if (src->closed) {
		spin_unlock(&src->lock);
		return;
	}
	spin_lock(&dst->lock);
	if (dst->closed) {
		warn("Destination closed before it opened");
		spin_unlock(&dst->lock);
		spin_unlock(&src->lock);
		return;
	}
	while (src->max_files > dst->max_files) {
		ret = grow_fd_set(dst);
		if (ret < 0) {
			set_error(-ret, "Failed to grow for a clone_fdt");
			spin_unlock(&dst->lock);
			spin_unlock(&src->lock);
			return;
		}
	}
	for (int i = 0; i < src->max_fdset; i++) {
		if (GET_BITMASK_BIT(src->open_fds->fds_bits, i)) {
			/* while max_files and max_fdset might not line up, we should never
			 * have a valid fdset higher than files */
			assert(i < src->max_files);
			chan = src->fd[i].fd_chan;
			assert(i < dst->max_files && dst->fd[i].fd_chan == 0);
			SET_BITMASK_BIT(dst->open_fds->fds_bits, i);
			dst->fd[i].fd_chan = chan;
			chan_incref(chan);
		}
	}
	dst->hint_min_fd = src->hint_min_fd;
	spin_unlock(&dst->lock);
	spin_unlock(&src->lock);
}

int fd_get_fd_flags(struct fd_table *fdt, int fd)
{
	int ret = -1;

	if (fd < 0)
		return -1;
	spin_lock(&fdt->lock);
	if (fdt->closed) {
		spin_unlock(&fdt->lock);
		return -1;
	}
	if ((fd < fdt->max_fdset) && GET_BITMASK_BIT(fdt->open_fds->fds_bits, fd))
		ret = fdt->fd[fd].fd_flags;
	spin_unlock(&fdt->lock);
	if (ret == -1)
		set_error(EBADF, "FD was not open");
	return ret;
}

int fd_set_fd_flags(struct fd_table *fdt, int fd, int new_fl)
{
	int ret = -1;

	if (fd < 0)
		return -1;
	spin_lock(&fdt->lock);
	if (fdt->closed) {
		spin_unlock(&fdt->lock);
		return -1;
	}
	if ((fd < fdt->max_fdset) && GET_BITMASK_BIT(fdt->open_fds->fds_bits, fd))
		fdt->fd[fd].fd_flags = new_fl;
	spin_unlock(&fdt->lock);
	if (ret == -1)
		set_error(EBADF, "FD was not open");
	return ret;
}
