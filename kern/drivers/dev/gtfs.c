/* Copyright (c) 2018 Google Inc
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * #gtfs, generic tree file system frontend that hooks to a backend 9p device
 */

#include <slab.h>
#include <kmalloc.h>
#include <kref.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <error.h>
#include <pmap.h>
#include <smp.h>
#include <tree_file.h>

struct dev gtfs_devtab;

static char *devname(void)
{
	return gtfs_devtab.name;
}

struct gtfs {
	struct tree_filesystem		tfs;
	struct kref					users;
};

/* Blob hanging off the fs_file->priv.  The backend chans are only accessed,
 * (changed or used) with the corresponding fs_file qlock held.  That's the
 * primary use of the qlock - we might be able to avoid qlocking with increfs
 * and atomics or spinlocks, but be careful of be_length.  Qlocking doesn't
 * matter much yet since #mnt serializes.
 *
 * The walk chan is never opened - it's basically just the walked fid, from
 * which we can do other walks or get the I/O chans.  The read and write chans
 * are opened on demand and closed periodically.  We open them initially on
 * open/create in case we are unable to open them (e.g. unwritable).  Better to
 * find out early than during a long writeback.
 *
 * The mnt server might complain about having too many open fids.  We can run a
 * ktask that periodically closes the be_chans on any LRU'd files.
 *
 * be_{length,mode,mtime} should be what the remote server thinks they are -
 * especially for length and mode.  The invariant is that e.g. the file's length
 * == be_length, and the qlock protects that invariant.  We don't care as much
 * about mtime, since some 9p servers just change that on their own. */
struct gtfs_priv {
	struct chan					*be_walk;	/* never opened */
	struct chan					*be_read;
	struct chan					*be_write;
	uint64_t					be_length;
	uint32_t					be_mode;
	struct timespec				be_mtime;
	bool						was_removed;
};

static inline struct gtfs_priv *fsf_to_gtfs_priv(struct fs_file *f)
{
	return f->priv;
}

static inline struct gtfs_priv *tf_to_gtfs_priv(struct tree_file *tf)
{
	return fsf_to_gtfs_priv(&tf->file);
}

/* Helper.  Clones the chan (walks to itself) and then opens with omode. */
static struct chan *cclone_and_open(struct chan *c, int omode)
{
	ERRSTACK(1);
	struct chan *new;

	new = cclone(c);
	if (waserror()) {
		cclose(new);
		nexterror();
	}
	new = devtab[new->type].open(new, omode);
	poperror();
	return new;
}

/* Send a wstat with the contents of dir for the file. */
static void wstat_dir(struct fs_file *f, struct dir *dir)
{
	ERRSTACK(1);
	struct gtfs_priv *gp = fsf_to_gtfs_priv(f);
	size_t sz;
	uint8_t *buf;

	sz = sizeD2M(dir);
	buf = kzmalloc(sz, MEM_WAIT);
	convD2M(dir, buf, sz);
	if (waserror()) {
		kfree(buf);
		nexterror();
	}
	devtab[gp->be_walk->type].wstat(gp->be_walk, buf, sz);
	kfree(buf);
	poperror();
}

/* Note we only track and thus change the following:
 * - length
 * - mode
 * - mtime (second granularity)
 * If we support chown, we'll have to do something else there.  See
 * fs_file_copy_from_dir(). */
static void sync_metadata(struct fs_file *f)
{
	ERRSTACK(1);
	struct gtfs_priv *gp = fsf_to_gtfs_priv(f);
	struct dir dir;
	bool send_it = false;

	qlock(&f->qlock);
	init_empty_dir(&dir);
	if (f->dir.length != gp->be_length) {
		dir.length = f->dir.length;
		send_it = true;
	}
	if (f->dir.mode != gp->be_mode) {
		dir.mode = f->dir.mode;
		send_it = true;
	}
	if (f->dir.mtime.tv_sec != gp->be_mtime.tv_sec) {
		/* ninep's UFS server assumes you set both atime and mtime */
		dir.atime.tv_sec = f->dir.atime.tv_sec;
		dir.atime.tv_nsec = f->dir.atime.tv_nsec;
		dir.mtime.tv_sec = f->dir.mtime.tv_sec;
		dir.mtime.tv_nsec = f->dir.mtime.tv_nsec;
		send_it = true;
	}
	if (!send_it) {
		qunlock(&f->qlock);
		return;
	}
	if (waserror()) {
		qunlock(&f->qlock);
		nexterror();
	}
	wstat_dir(f, &dir);
	/* We set these after the wstat succeeds.  If we set them earlier, we'd have
	 * to roll back.  Remember the invariant: the be_values match the backend's
	 * file's values.  We should be able to stat be_walk and check these (though
	 * the 9p server might muck with atime/mtime). */
	if (f->dir.length != gp->be_length)
		gp->be_length = f->dir.length;
	if (f->dir.mode != gp->be_mode)
		gp->be_mode = f->dir.mode;
	if (f->dir.mtime.tv_sec != gp->be_mtime.tv_sec)
		gp->be_mtime = f->dir.mtime;
	qunlock(&f->qlock);
	poperror();
}

/* Can throw on error, currently from sync_metadata. */
static void writeback_file(struct fs_file *f)
{
	sync_metadata(f);
	/* This is a lockless peak.  Once a file is dirtied, we never undirty it.
	 * To do so, we need the file qlock (not a big deal, though that may replace
	 * the PM qlock), and we still need to handle/scan mmaps.  Specifically, we
	 * only dirty when an mmap attaches (PROT_WRITE and MAP_SHARED), but we
	 * don't know if an existing mapping has caused more dirtying (an mmap can
	 * re-dirty then detach before our next writeback).  That usually requires a
	 * scan.  This is all an optimization to avoid scanning the entire PM's
	 * pages for whether or not they are dirty.
	 *
	 * Also, our writeback pm op grabs the file's qlock.  So be careful; though
	 * we could use another qlock, since we're mostly protecting backend state.
	 */
	if (qid_is_file(f->dir.qid) && (f->flags & FSF_DIRTY))
		pm_writeback_pages(f->pm);
}

static void purge_cb(struct tree_file *tf)
{
	ERRSTACK(1)

	/* discard error, and keep on going if we can. */
	if (!waserror())
		writeback_file(&tf->file);
	poperror();
}

static void gtfs_release(struct kref *kref)
{
	struct gtfs *gtfs = container_of(kref, struct gtfs, users);

	tfs_frontend_purge(&gtfs->tfs, purge_cb);
	/* this is the ref from attach */
	assert(kref_refcnt(&gtfs->tfs.root->kref) == 1);
	tf_kref_put(gtfs->tfs.root);
	/* ensures __tf_free() happens before tfs_destroy */
	rcu_barrier();
	tfs_destroy(&gtfs->tfs);
	kfree(gtfs);
}

static struct gtfs *chan_to_gtfs(struct chan *c)
{
	struct tree_file *tf = chan_to_tree_file(c);

	return (struct gtfs*)(tf->tfs);
}

static void incref_gtfs_chan(struct chan *c)
{
	kref_get(&chan_to_gtfs(c)->users, 1);
}

static void decref_gtfs_chan(struct chan *c)
{
	kref_put(&chan_to_gtfs(c)->users);
}

static struct walkqid *gtfs_walk(struct chan *c, struct chan *nc, char **name,
                                 unsigned int nname)
{
	struct walkqid *wq;

	wq = tree_chan_walk(c, nc, name, nname);
	if (wq && wq->clone && (wq->clone != c))
		incref_gtfs_chan(wq->clone);
	return wq;
}

/* Given an omode, make sure the be chans are set up */
static void setup_be_chans(struct chan *c, int omode)
{
	ERRSTACK(1);
	struct tree_file *tf = chan_to_tree_file(c);
	struct fs_file *f = &tf->file;
	struct gtfs_priv *gp = fsf_to_gtfs_priv(f);

	qlock(&f->qlock);
	if (waserror()) {
		qunlock(&f->qlock);
		nexterror();
	}
	/* Readers and writers both need be_read.  With fs files you can't have a
	 * writable-only file, since we need to load the page into the page cache,
	 * which is a readpage. */
	if (!gp->be_read)
		gp->be_read = cclone_and_open(gp->be_walk, O_READ);
	if (!gp->be_write && (omode & O_WRITE))
		gp->be_write = cclone_and_open(gp->be_walk, O_WRITE);
	qunlock(&f->qlock);
	poperror();
}

static struct chan *gtfs_open(struct chan *c, int omode)
{
	/* truncate can happen before we setup the be_chans.  if we need those, we
	 * can swap the order */
	c = tree_chan_open(c, omode);
	setup_be_chans(c, omode);
	return c;
}


static void gtfs_create(struct chan *c, char *name, int omode, uint32_t perm,
                        char *ext)
{
	tree_chan_create(c, name, omode, perm, ext);
	/* We have to setup *after* create, since it moves the chan from the parent
	 * to the new file. */
	setup_be_chans(c, omode);
}

static void gtfs_close(struct chan *c)
{
	tree_chan_close(c);
	decref_gtfs_chan(c);
}

static void gtfs_remove(struct chan *c)
{
	ERRSTACK(1);
	struct gtfs *gtfs = chan_to_gtfs(c);

	if (waserror()) {
		/* Same old pain-in-the-ass for remove */
		kref_put(&gtfs->users);
		nexterror();
	}
	tree_chan_remove(c);
	kref_put(&gtfs->users);
	poperror();
}

static size_t gtfs_wstat(struct chan *c, uint8_t *m_buf, size_t m_buf_sz)
{
	size_t ret;

	ret = tree_chan_wstat(c, m_buf, m_buf_sz);
	/* Tell the backend so that any metadata changes take effect immediately.
	 * Consider chmod +w.  We need to tell the 9p server so that it will allow
	 * future accesses. */
	sync_metadata(&chan_to_tree_file(c)->file);
	return ret;
}

/* Caller holds the file's qlock. */
static size_t __gtfs_fsf_read(struct fs_file *f, void *ubuf, size_t n,
                              off64_t off)
{
	struct gtfs_priv *gp = fsf_to_gtfs_priv(f);

	if (off >= gp->be_length) {
		/* We can skip the RPC, since we know it will return zero (EOF). */
		return 0;
	}
	if (!gp->be_read)
		gp->be_read = cclone_and_open(gp->be_walk, O_READ);
	return devtab[gp->be_read->type].read(gp->be_read, ubuf, n, off);
}

/* Reads a file from its backend chan */
static size_t gtfs_fsf_read(struct fs_file *f, void *ubuf, size_t n,
                            off64_t off)
{
	ERRSTACK(1);
	size_t ret;

	qlock(&f->qlock);
	if (waserror()) {
		qunlock(&f->qlock);
		nexterror();
	}
	ret = __gtfs_fsf_read(f, ubuf, n, off);
	qunlock(&f->qlock);
	poperror();
	return ret;
}

/* Caller holds the file's qlock. */
static size_t __gtfs_fsf_write(struct fs_file *f, void *ubuf, size_t n,
                               off64_t off)
{
	struct gtfs_priv *gp = fsf_to_gtfs_priv(f);
	size_t ret;

	if (!gp->be_write)
		gp->be_write = cclone_and_open(gp->be_walk, O_WRITE);
	ret = devtab[gp->be_write->type].write(gp->be_write, ubuf, n, off);
	gp->be_length = MAX(gp->be_length, n + ret);
	return ret;
}

/* Writes a file to its backend chan */
static size_t gtfs_fsf_write(struct fs_file *f, void *ubuf, size_t n,
                             off64_t off)
{
	ERRSTACK(1);
	size_t ret;

	qlock(&f->qlock);
	if (waserror()) {
		qunlock(&f->qlock);
		nexterror();
	}
	ret = __gtfs_fsf_write(f, ubuf, n, off);
	qunlock(&f->qlock);
	poperror();
	return ret;
}

static size_t gtfs_read(struct chan *c, void *ubuf, size_t n, off64_t off)
{
	struct tree_file *tf = chan_to_tree_file(c);

	if (tree_file_is_dir(tf))
		return gtfs_fsf_read(&tf->file, ubuf, n, off);
	return fs_file_read(&tf->file, ubuf, n, off);
}

/* Given a file (with dir->name set), couple it and sync to the backend chan.
 * This will store/consume the ref for backend, in the TF (freed with
 * gtfs_tf_free), even on error, unless you zero out the be_walk field. */
static void gtfs_tf_couple_backend(struct tree_file *tf, struct chan *backend)
{
	struct dir *dir;
	struct gtfs_priv *gp = kzmalloc(sizeof(struct gtfs_priv), MEM_WAIT);

	tf->file.priv = gp;
	tf->file.dir.qid = backend->qid;
	gp->be_walk = backend;
	dir = chandirstat(backend);
	if (!dir)
		error(ENOMEM, "chandirstat failed");
	fs_file_copy_from_dir(&tf->file, dir);
	kfree(dir);
	/* For sync_metadata */
	gp->be_length = tf->file.dir.length;
	gp->be_mode = tf->file.dir.mode;
	gp->be_mtime = tf->file.dir.mtime;
}

static void gtfs_tf_free(struct tree_file *tf)
{
	struct gtfs_priv *gp = tf_to_gtfs_priv(tf);

	/* Might have some partially / never constructed tree files */
	if (!gp)
		return;
	if (gp->was_removed) {
		gp->be_walk->type = -1;
		/* sanity */
		assert(kref_refcnt(&gp->be_walk->ref) == 1);
	}
	cclose(gp->be_walk);
	/* I/O chans can be NULL */
	cclose(gp->be_read);
	cclose(gp->be_write);
	kfree(gp);
}

static void gtfs_tf_unlink(struct tree_file *parent, struct tree_file *child)
{
	struct gtfs_priv *gp = tf_to_gtfs_priv(child);
	struct chan *be_walk = gp->be_walk;

	/* Remove clunks the be_walk chan/fid.  if it succeeded (and I think even if
	 * it didn't), we shouldn't close that fid again, which is what will happen
	 * soon after this function.  The TF code calls unlink, then when the last
	 * ref closes the TF, it'll get freed and we'll call back to gtfs_tf_free().
	 *
	 * This is the same issue we run into with all of the device remove ops
	 * where we want to refcnt something hanging off e.g. c->aux.  In 9p, you're
	 * not supposed to close a chan/fid that was already removed.
	 *
	 * Now here's the weird thing.  We can close the be_walk chan after remove,
	 * but it's possible that someone has walked and perhaps opened a frontend
	 * chan + TF, but hasn't done a read yet.  So someone might want to set up
	 * be_read, but they can't due to be_walk being closed.  We could give them
	 * a 'phase error' (one of 9p's errors for I/O on a removed file).
	 *
	 * Alternatively, we can mark the gtfs_priv so that when we do free it, we
	 * skip the dev.remove, similar to what sysremove() does.  That's probably
	 * easier.  This is technically racy, but we know that the release/free
	 * method won't be called until we return. */
	gp->was_removed = true;
	devtab[be_walk->type].remove(be_walk);
}

/* Caller sets the name, but doesn't know if it exists or not.  It's our job to
 * find out if it exists and fill in the child structure appropriately.  For
 * negative entries, just flagging it is fine.  Otherwise, we fill in the dir.
 * We should throw on error. */
static void gtfs_tf_lookup(struct tree_file *parent, struct tree_file *child)
{
	struct walkqid *wq;
	struct chan *be_walk = tf_to_gtfs_priv(parent)->be_walk;
	struct chan *child_be_walk;

	wq = devtab[be_walk->type].walk(be_walk, NULL, &child->file.dir.name, 1);
	if (!wq || !wq->clone) {
		kfree(wq);
		/* This isn't racy, since the child isn't linked to the tree yet */
		child->flags |= TF_F_NEGATIVE | TF_F_HAS_BEEN_USED;
		return;
	}
	/* walk shouldn't give us the same chan struct since we gave it a name and a
	 * NULL nc. */
	assert(wq->clone != be_walk);
	/* only gave it one name, and it didn't fail. */
	assert(wq->nqid == 1);
	/* sanity */
	assert(wq->clone->qid.path == wq->qid[wq->nqid - 1].path);
	child_be_walk = wq->clone;
	kfree(wq);
	gtfs_tf_couple_backend(child, child_be_walk);
}

static void gtfs_tf_create(struct tree_file *parent, struct tree_file *child,
                           int perm)
{
	ERRSTACK(1);
	struct chan *c = cclone(tf_to_gtfs_priv(parent)->be_walk);

	if (waserror()) {
		cclose(c);
		nexterror();
	}
	devtab[c->type].create(c, tree_file_to_name(child), 0, perm,
	                       child->file.dir.ext);
	/* The chan c is opened, which we don't want.  We can't cclone it either
	 * (since it is opened).  All we can do is have the parent walk again so we
	 * can get the child's unopened be_walk chan.  Conveniently, that's
	 * basically a lookup, so create is really two things: make it, then look it
	 * up from the backend. */
	cclose(c);
	poperror();
	if (waserror()) {
		warn("File %s was created in the backend, but unable to look it up!",
		     tree_file_to_name(child));
		nexterror();
	}
	gtfs_tf_lookup(parent, child);
	poperror();
}

static void gtfs_tf_rename(struct tree_file *tf, struct tree_file *old_parent,
                           struct tree_file *new_parent, const char *name,
                           int flags)
{
	struct chan *tf_c = tf_to_gtfs_priv(tf)->be_walk;
	struct chan *np_c = tf_to_gtfs_priv(new_parent)->be_walk;

	if (!devtab[tf_c->type].rename)
		error(EXDEV, "%s: %s doesn't support rename", devname(),
		      devtab[tf_c->type].name);
	devtab[tf_c->type].rename(tf_c, np_c, name, flags);
}

static bool gtfs_tf_has_children(struct tree_file *parent)
{
	struct dir dir[1];

	assert(tree_file_is_dir(parent));	/* TF bug */
	/* Any read should work, but there might be issues asking for something
	 * smaller than a dir.
	 *
	 * Note we use the unlocked read here.  The fs_file's qlock is held by our
	 * caller, and we reuse that qlock for the sync for reading/writing. */
	return __gtfs_fsf_read(&parent->file, dir, sizeof(struct dir), 0) > 0;
}

struct tree_file_ops gtfs_tf_ops = {
	.free = gtfs_tf_free,
	.unlink = gtfs_tf_unlink,
	.lookup = gtfs_tf_lookup,
	.create = gtfs_tf_create,
	.rename = gtfs_tf_rename,
	.has_children = gtfs_tf_has_children,
};

/* Fills page with its contents from its backing store file.
 *
 * Note the page/offset might be beyond the current file length, based on the
 * current pagemap code. */
static int gtfs_pm_readpage(struct page_map *pm, struct page *pg)
{
	ERRSTACK(1);
	void *kva = page2kva(pg);
	off64_t offset = pg->pg_index << PGSHIFT;
	size_t ret;

	if (waserror()) {
		poperror();
		return -get_errno();
	}
	/* If offset is beyond the length of the file, the 9p device/server should
	 * return 0.  We'll just init an empty page.  The length on the frontend (in
	 * the fsf->dir.length) will be adjusted.  The backend will hear about it on
	 * the next sync. */
	ret = gtfs_fsf_read(pm->pm_file, kva, PGSIZE, offset);
	poperror();
	if (ret < PGSIZE)
		memset(kva + ret, 0, PGSIZE - ret);
	atomic_or(&pg->pg_flags, PG_UPTODATE);
	return 0;
}

/* Meant to take the page from PM and flush to backing store. */
static int gtfs_pm_writepage(struct page_map *pm, struct page *pg)
{
	ERRSTACK(1);
	struct fs_file *f = pm->pm_file;
	void *kva = page2kva(pg);
	off64_t offset = pg->pg_index << PGSHIFT;
	size_t amt;

	qlock(&f->qlock);
	if (waserror()) {
		qunlock(&f->qlock);
		poperror();
		return -get_errno();
	}
	/* Don't writeback beyond the length of the file.  Most of the time this
	 * comes up is when the len is in the middle of the last page. */
	if (offset >= fs_file_get_length(f)) {
		qunlock(&f->qlock);
		return 0;
	}
	amt = MIN(PGSIZE, fs_file_get_length(f) - offset);
	__gtfs_fsf_write(f, kva, amt, offset);
	qunlock(&f->qlock);
	poperror();
	return 0;
}

/* Caller holds the file's qlock */
static void __trunc_to(struct fs_file *f, off64_t begin)
{
	struct gtfs_priv *gp = fsf_to_gtfs_priv(f);
	struct dir dir;

	init_empty_dir(&dir);
	dir.length = begin;
	wstat_dir(f, &dir);
	/* recall the invariant: be_length == the backend's length */
	gp->be_length = begin;
}

/* Caller holds the file's qlock */
static void __zero_fill(struct fs_file *f, off64_t begin, off64_t end)
{
	ERRSTACK(1);
	void *zeros;

	if (PGOFF(begin) || PGOFF(end))
		error(EINVAL, "zero_fill had unaligned begin (%p) or end (%p)\n",
		      begin, end);
	zeros = kpages_zalloc(PGSIZE, MEM_WAIT);
	if (waserror()) {
		kpages_free(zeros, PGSIZE);
		nexterror();
	}
	for (off64_t o = begin; o < end; o += PGSIZE)
		__gtfs_fsf_write(f, zeros, PGSIZE, o);
	poperror();
}

/* The intent here is for the backend to drop all data in the range.  Zeros are
 * OK - any future read should get a zero.
 *
 * These offsets are the beginning and end of the hole to punch.  The TF code
 * already dealt with edge cases, so these happen to be page aligned.  That
 * shouldn't matter for the backend device.
 *
 * Don't worry about a border page for end that is all zeros.
 * fs_file_truncate() rounded up to the nearest page to avoid issues.  The user
 * could manually punch a hole, and they could create a page of zeros at end.
 * We don't care.
 *
 * 9p doesn't have a hole-punch, so we'll truncate if we can and o/w fill with
 * zeros.
 *
 * Note that the frontend's file length often differs from the backend.  Under
 * normal operation, such as writing to a file, the frontend's len will be
 * greater than the backend's.  When we sync, the backend learns the real
 * length.  Similarly, when we shrink a gile, the backend's length may be
 * greater than the frontend.  Consider a truncate from 8192 to 4095: we punch
 * with begin = 4096, end = 8192.  In either case, the backend learns the real
 * length on a sync.  In punch_hole, we're just trying to discard old data. */
static void gtfs_fs_punch_hole(struct fs_file *f, off64_t begin, off64_t end)
{
	ERRSTACK(1);
	struct gtfs_priv *gp = fsf_to_gtfs_priv(f);

	qlock(&f->qlock);
	if (waserror()) {
		qunlock(&f->qlock);
		nexterror();
	}
	if (end >= gp->be_length) {
		if (begin < gp->be_length)
			__trunc_to(f, begin);
	} else {
		__zero_fill(f, begin, end);
	}
	qunlock(&f->qlock);
	poperror();
}

static bool gtfs_fs_can_grow_to(struct fs_file *f, size_t len)
{
	/* TODO: are there any limits in 9p? */
	return true;
}

struct fs_file_ops gtfs_fs_ops = {
	.readpage = gtfs_pm_readpage,
	.writepage = gtfs_pm_writepage,
	.punch_hole = gtfs_fs_punch_hole,
	.can_grow_to = gtfs_fs_can_grow_to,
};

/* We're passed a backend chan, usually of type #mnt, used for an uncached
 * mount.  We call it 'backend.'  It is the result of an attach, e.g. mntattach.
 * In the case of #mnt, this chan is different than the one that has the 9p
 * server on the other side, called 'mchan'.  That chan is at backend->mchan,
 * and also the struct mnt->c.  The struct mnt is shared by all mounts talking
 * to the 9p server over the mchan, and is stored at mchan->mux.  Backend chans
 * have a strong (counted) ref on the mchan.
 *
 * We create and return a chan of #gtfs, suitable for attaching to the
 * namespace.  This chan will have the root TF hanging off aux, just like how
 * any other attached TFS has a root TF.  #gtfs manages the linkage between a TF
 * and the backend, which is the purpose of gtfs_priv.
 *
 * A note on refcounts: in the normal, uncached operation, the 'backend' chan
 * has a ref (actually a chan kref, which you cclose) on the comms chan (mchan).
 * We get one ref at mntattach time, and every distinct mntwalk gets another
 * ref.  Those actually get closed in chanfree(), since they are stored at
 * mchan.
 *
 * All gtfs *tree_files* have at least one refcounted chan corresponding to the
 * file/FID on the backend server.  Think of it as a 1:1 connection, even though
 * there is more than one chan.  The gtfs device can have many chans pointing to
 * the same TF, which is kreffed.  That TF is 1:1 on a backend object.
 *
 * All walks from this attach point will get chans with TFs from this TFS and
 * will incref the struct gtfs.
 */
static struct chan *gtfs_attach(char *arg)
{
	ERRSTACK(2);
	struct chan *backend = (struct chan*)arg;
	struct chan *frontend;
	struct tree_filesystem *tfs;
	struct gtfs *gtfs;

	frontend = devattach(devname(), 0);
	if (waserror()) {
		/* same as #mnt - don't cclose, since we don't want to devtab close, and
		 * we know the ref == 1 here. */
		chanfree(frontend);
		nexterror();
	}
	gtfs = kzmalloc(sizeof(struct gtfs), MEM_WAIT);
	/* This 'users' kref is the one that every distinct frontend chan has.
	 * These come from attaches and successful, 'moving' walks. */
	kref_init(&gtfs->users, gtfs_release, 1);
	tfs = (struct tree_filesystem*)gtfs;
	/* This gives us one ref on root, released during gtfs_release().  name is
	 * set to ".", though that gets overwritten during coupling. */
	tfs_init(tfs);
	if (waserror()) {
		/* don't consume the backend ref on error, caller expects to have it */
		tf_to_gtfs_priv(tfs->root)->be_walk = NULL;
		/* ref from tfs_init.  this should free the TF. */
		tf_kref_put(tfs->root);
		tfs_destroy(tfs);
		kfree(gtfs);
		nexterror();
	}
	/* stores the ref for 'backend' inside tfs->root */
	gtfs_tf_couple_backend(tfs->root, backend);
	poperror();
	tfs->tf_ops = gtfs_tf_ops;
	tfs->fs_ops = gtfs_fs_ops;
	/* need another ref on root for the frontend chan */
	tf_kref_get(tfs->root);
	chan_set_tree_file(frontend, tfs->root);
	poperror();
	return frontend;
}

static bool lru_prune_cb(struct tree_file *tf)
{
	ERRSTACK(1);

	if (waserror()) {
		/* not much to do - ssh the file out? */
		printk("Failed to sync file %s: %s\n", tree_file_to_name(tf),
		       current_errstr());
		poperror();
		return false;
	}
	writeback_file(&tf->file);
	poperror();
	return true;
}

static void pressure_dfs_cb(struct tree_file *tf)
{
	if (!tree_file_is_dir(tf))
		pm_free_unused_pages(tf->file.pm);
}

/* Under memory pressure, there are a bunch of things we can do. */
static void gtfs_free_memory(struct gtfs *gtfs)
{
	/* This attempts to remove every file from the LRU.  It'll write back dirty
	 * files, then if they haven't been used since we started, it'll delete the
	 * frontend TF, which will delete the entire page cache entry.  The heavy
	 * lifting is done by TF code. */
	tfs_lru_for_each(&gtfs->tfs, lru_prune_cb, -1);
	/* This drops the negative TFs.  It's not a huge deal, since they are small,
	 * but perhaps it'll help. */
	tfs_lru_prune_neg(&gtfs->tfs);
	/* This will attempt to free memory from all files in the frontend,
	 * regardless of whether or not they are in use.  This might help if you
	 * have some large files that happened to be open. */
	tfs_frontend_for_each(&gtfs->tfs, pressure_dfs_cb);
}

static void gtfs_sync_tf(struct tree_file *tf)
{
	writeback_file(&tf->file);
}

static void gtfs_sync_gtfs(struct gtfs *gtfs)
{
	tfs_frontend_for_each(&gtfs->tfs, gtfs_sync_tf);
}

/* chan_ctl or something can hook into these functions */
static void gtfs_sync_chan(struct chan *c)
{
	gtfs_sync_tf(chan_to_tree_file(c));
}

static void gtfs_sync_chans_fs(struct chan *any_c)
{
	gtfs_sync_gtfs(chan_to_gtfs(any_c));
}

static unsigned long gtfs_chan_ctl(struct chan *c, int op, unsigned long a1,
                                   unsigned long a2, unsigned long a3,
                                   unsigned long a4)
{
	switch (op) {
	case CCTL_SYNC:
		if (tree_file_is_dir(chan_to_tree_file(c)))
			gtfs_sync_chans_fs(c);
		else
			gtfs_sync_chan(c);
		return 0;
	default:
		error(EINVAL, "%s does not support %d", __func__, op);
	}
}

struct dev gtfs_devtab __devtab = {
	.name = "gtfs",

	.reset = devreset,
	.init = devinit,
	.shutdown = devshutdown,
	.attach = gtfs_attach,
	.walk = gtfs_walk,
	.stat = tree_chan_stat,
	.open = gtfs_open,
	.create = gtfs_create,
	.close = gtfs_close,
	.read = gtfs_read,
	.bread = devbread,
	.write = tree_chan_write,
	.bwrite = devbwrite,
	.remove = gtfs_remove,
	.wstat = gtfs_wstat,
	.power = devpower,
	.chaninfo = devchaninfo,
	.mmap = tree_chan_mmap,
	.chan_ctl = gtfs_chan_ctl,
};
