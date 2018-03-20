/* Copyright (c) 2018 Google Inc
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * fs_file: structs and helpers for files for 9ns devices
 */

#include <fs_file.h>
#include <kmalloc.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <error.h>
#include <umem.h>
#include <pmap.h>

/* Initializes a zalloced fs_file.  The caller is responsible for filling in
 * dir, except for name.  Most fields are fine with being zeroed.  Note the kref
 * == 0 too. */
void fs_file_init(struct fs_file *f, const char *name, struct fs_file_ops *ops)
{
	qlock_init(&f->qlock);
	fs_file_set_basename(f, name);
	f->ops = ops;
	/* TODO: consider holding off on initializing the PM, since only walked and
	 * opened entries could use it.  pm == NULL means no PM yet.  Negative
	 * entries will never be used in this manner.  Doing it now avoids races,
	 * though it's mostly zeroing cache-hot fields. */
	f->pm = &f->static_pm;
	pm_init(f->pm, (struct page_map_operations*)ops, f);
}

void fs_file_set_basename(struct fs_file *f, const char *name)
{
	size_t name_len = strlen(name) + 1;

	if (name_len > KNAMELEN)
		f->dir.name = kzmalloc(name_len, MEM_WAIT);
	else
		f->dir.name = f->static_name;
	memcpy(f->dir.name, name, name_len);
}

/* Technically, a reader could see the old string pointer and read it.  That
 * memory could be realloced and used for something else.  But thanks to the
 * seqctr, the reader will retry.  Otherwise, we might not need the seqctr,
 * since we never change_basename when a file is in a tree.  So far.
 *
 * The only reader that races with setting the name is stat.  Regular lookups
 * won't see the file, since it was removed from the HT, and readdirs won't see
 * it due to the parent's qlock. */
void fs_file_change_basename(struct fs_file *f, const char *name)
{
	char *old_name = NULL;
	char *new_name = NULL;
	size_t name_len = strlen(name) + 1;

	if (name_len > KNAMELEN)
		new_name = kzmalloc(name_len, MEM_WAIT);
	qlock(&f->qlock);
	if (f->dir.name != f->static_name)
		old_name = f->dir.name;
	if (new_name)
		f->dir.name = new_name;
	else
		f->dir.name = f->static_name;
	memcpy(f->dir.name, name, name_len);
	/* TODO: if we store the hash of the name in the file, do so here. */
	qunlock(&f->qlock);
	kfree(old_name);
}

/* Helper for building a dir.  Caller sets qid path and vers.  YMMV. */
void fs_file_init_dir(struct fs_file *f, int dir_type, int dir_dev,
                      struct username *user, int perm)
{
	struct dir *dir = &f->dir;

	if (perm & DMDIR)
		dir->qid.type |= QTDIR;
	if (perm & DMAPPEND)
		dir->qid.type |= QTAPPEND;
	if (perm & DMEXCL)
		dir->qid.type |= QTEXCL;
	if (perm & DMSYMLINK)
		dir->qid.type |= QTSYMLINK;
	/* dir->mode stores all the DM bits, but note that userspace can only affect
	 * the permissions (S_PMASK) bits. */
	dir->mode = perm;
	__set_acmtime(f, FSF_ATIME | FSF_BTIME | FSF_MTIME | FSF_CTIME);
	dir->length = 0;
	/* TODO: this is a mess if you use anything other than eve.  If you use a
	 * process, that memory is sitting in the proc struct, but we have weak refs
	 * on it.  What happens when that proc exits?  Disaster. */
	assert(user == &eve);
	dir->uid = user->name;
	dir->gid = user->name;
	dir->muid = user->name;
}

static char *copy_str(const char *s)
{
	char *ret;
	size_t sz;

	if (!s)
		return NULL;
	sz = strlen(s) + 1;
	ret = kmalloc(sz, MEM_WAIT);
	memcpy(ret, s, sz);
	return ret;
}

/* Deep copies the contents of dir into the fs_file's dir. */
void fs_file_copy_from_dir(struct fs_file *f, struct dir *dir)
{
	memcpy(&f->dir, dir, sizeof(struct dir));
	fs_file_set_basename(f, dir->name);
	/* TODO: sort out usernames.  Not only are these just eve, but they are not
	 * struct user or something and they ignore whatever the name was from the
	 * remote end. */
	f->dir.uid = eve.name;
	f->dir.gid = eve.name;
	f->dir.muid = eve.name;
	f->dir.ext = copy_str(dir->ext);
}

void cleanup_fs_file(struct fs_file *f)
{
	if (f->dir.name != f->static_name)
		kfree(f->dir.name);
	/* TODO: Not sure if these will be refcounted objects in the future.  Keep
	 * this in sync with other code that manages/sets uid/gid/muid. */
	f->dir.uid = NULL;
	f->dir.gid = NULL;
	f->dir.muid = NULL;
	if (f->dir.ext)
		kfree(f->dir.ext);
	f->dir.ext = NULL;
	pm_destroy(f->pm);
	/* Might share mappings in the future.  Catch it here. */
	assert(f->pm == &f->static_pm);
}

void __set_acmtime_to(struct fs_file *f, int which, struct timespec *t)
{
	/* WRITE_ONCE, due to lockless peakers */
	if (which & FSF_ATIME) {
		WRITE_ONCE(f->dir.atime.tv_sec, t->tv_sec);
		WRITE_ONCE(f->dir.atime.tv_nsec, t->tv_nsec);
	}
	if (which & FSF_BTIME) {
		WRITE_ONCE(f->dir.btime.tv_sec, t->tv_sec);
		WRITE_ONCE(f->dir.btime.tv_nsec, t->tv_nsec);
	}
	if (which & FSF_CTIME) {
		WRITE_ONCE(f->dir.ctime.tv_sec, t->tv_sec);
		WRITE_ONCE(f->dir.ctime.tv_nsec, t->tv_nsec);
	}
	if (which & FSF_MTIME) {
		WRITE_ONCE(f->dir.mtime.tv_sec, t->tv_sec);
		WRITE_ONCE(f->dir.mtime.tv_nsec, t->tv_nsec);
	}
}

/* Caller should hold f's qlock */
void __set_acmtime(struct fs_file *f, int which)
{
	struct timespec now = nsec2timespec(epoch_nsec());

	__set_acmtime_to(f, which, &now);
}

/* Recall that the frontend always has the most up-to-date info.  This gets
 * synced to the backend when we flush or fsync. */
void set_acmtime_to(struct fs_file *f, int which, struct timespec *t)
{
	ERRSTACK(1);

	qlock(&f->qlock);
	if (waserror()) {
		qunlock(&f->qlock);
		nexterror();
	}
	if ((which & FSF_ATIME) && !caller_has_file_perms(f, O_READ))
		error(EPERM, "insufficient perms to set atime");
	if ((which & FSF_BTIME) && !caller_is_username(f->dir.uid))
		error(EPERM, "insufficient perms to set btime");
	if ((which & FSF_CTIME) && !caller_has_file_perms(f, O_WRITE))
		error(EPERM, "insufficient perms to set ctime");
	if ((which & FSF_MTIME) && !caller_has_file_perms(f, O_WRITE))
		error(EPERM, "insufficient perms to set mtime");
	__set_acmtime_to(f, which, t);
	qunlock(&f->qlock);
	poperror();
}

void set_acmtime_noperm(struct fs_file *f, int which)
{
	struct timespec now = nsec2timespec(epoch_nsec());

	/* <3 atime.  We'll go with an hour resolution, like NTFS. */
	if (which == FSF_ATIME) {
		if (now.tv_sec < ACCESS_ONCE(f->dir.atime.tv_sec) + 3600)
			return;
	}
	qlock(&f->qlock);
	__set_acmtime_to(f, which, &now);
	qunlock(&f->qlock);
}

size_t fs_file_stat(struct fs_file *f, uint8_t *m_buf, size_t m_buf_sz)
{
	size_t ret;

	qlock(&f->qlock);
	ret = convD2M(&f->dir, m_buf, m_buf_sz);
	qunlock(&f->qlock);
	if (ret <= BIT16SZ)
		error(EINVAL, "buffer too small for stat");
	return ret;
}

/* Punches a hole from begin to end.  Pages completely in the hole will be
 * removed.  Otherwise, the edges will be zeroed.  Caller holds the qlock. */
static void __fs_file_punch_hole(struct fs_file *f, off64_t begin, off64_t end)
{
	size_t first_pg_idx, last_pg_idx, nr_pages, zero_amt;
	struct page *page;
	int error;

	/* Caller should check for this */
	assert((long)begin >= 0);
	assert((long)end >= 0);
	end = MIN(end, f->dir.length);
	if (end <= begin)
		return;
	first_pg_idx = LA2PPN(begin);
	last_pg_idx = LA2PPN(end);
	nr_pages = last_pg_idx - first_pg_idx + 1;
	if (PGOFF(begin)) {
		error = pm_load_page(f->pm, first_pg_idx, &page);
		if (error)
			error(-error, "punch_hole pm_load_page failed");
		zero_amt = MIN(PGSIZE - PGOFF(begin), end - begin);
		memset(page2kva(page) + PGOFF(begin), 0, zero_amt);
		atomic_or(&page->pg_flags, PG_DIRTY);
		pm_put_page(page);
		first_pg_idx--;
		nr_pages--;
		if (!nr_pages)
			return;
	}
	if (PGOFF(end) && (end < f->dir.length)) {
		error = pm_load_page(f->pm, last_pg_idx, &page);
		if (error)
			error(-error, "punch_hole pm_load_page failed");
		memset(page2kva(page), 0, PGOFF(end));
		atomic_or(&page->pg_flags, PG_DIRTY);
		pm_put_page(page);
		nr_pages--;
		if (!nr_pages)
			return;
	}
	/* We hold the qlock, so new pages won't get loaded (load_nowait will fail).
	 * However, this won't remove all pages yet! (see PM comments above) */
	pm_remove_contig(f->pm, first_pg_idx, nr_pages);
	/* Need to do an FS op to immediately tell the backing store before we read
	 * a page back in to the PM.  Otherwise, we might see old data.  Consider a
	 * truncate to 0, then back to len. */
	f->ops->punch_hole(f, begin, end);
}

void fs_file_truncate(struct fs_file *f, off64_t to)
{
	ERRSTACK(1);

	fs_file_perm_check(f, O_WRITE);
	qlock(&f->qlock);
	if (waserror()) {
		qunlock(&f->qlock);
		nexterror();
	}
	if (to < f->dir.length) {
		__fs_file_punch_hole(f, to, f->dir.length);
	} else {
		if (!f->ops->can_grow_to(f, to))
			error(EINVAL, "can't grow file to %lu bytes", to);
	}
	WRITE_ONCE(f->dir.length, to);
	__set_acmtime(f, FSF_MTIME | FSF_CTIME);
	qunlock(&f->qlock);
	poperror();
}

/* This attempts to avoid the file qlock when pages are in the page cache.
 *
 * If we want to go simpler and use the qlock, always check len, don't use
 * _nowait, it's not a 'lockless peak', write can set the len at the top, and
 * all writes to dir.length can be normal. */
size_t fs_file_read(struct fs_file *f, uint8_t *buf, size_t count,
                    off64_t offset)
{
	ERRSTACK(1);
	struct page *page;
	size_t copy_amt, pg_off, pg_idx, total_remaining;
	volatile size_t so_far = 0;		/* volatile for waserror */
	const uint8_t *buf_end = buf + count;
	int error;

	if (waserror()) {
		if (so_far) {
			poperror();
			return so_far;
		}
		nexterror();
	}
	while (buf < buf_end) {
		pg_off = PGOFF(offset + so_far);
		pg_idx = LA2PPN(offset + so_far);
		error = pm_load_page_nowait(f->pm, pg_idx, &page);
		if (error == -EAGAIN) {
			qlock(&f->qlock);
			/* Don't attempt to load pages beyond len.  The rest of the read
			 * code can handle it (it won't copy), but the backend FS might get
			 * confused. */
			if (f->dir.length <= offset + so_far) {
				qunlock(&f->qlock);
				break;
			}
			error = pm_load_page(f->pm, pg_idx, &page);
			qunlock(&f->qlock);
		}
		if (error)
			error(-error, "read pm_load_page failed");
		copy_amt = MIN(PGSIZE - pg_off, buf_end - buf);
		/* Lockless peak.  Check the len so we don't read beyond EOF.  We have a
		 * page, but we don't necessarily have access to all of it. */
		total_remaining = ACCESS_ONCE(f->dir.length) - (offset + so_far);
		if (copy_amt > total_remaining) {
			copy_amt = total_remaining;
			buf_end = buf + copy_amt;
		}
		memcpy_to_safe(buf, page2kva(page) + pg_off, copy_amt);
		buf += copy_amt;
		so_far += copy_amt;
		pm_put_page(page);
	}
	if (so_far)
		set_acmtime_noperm(f, FSF_ATIME);
	poperror();
	return so_far;
}

size_t fs_file_write(struct fs_file *f, const uint8_t *buf, size_t count,
                     off64_t offset)
{
	ERRSTACK(1);
	struct page *page;
	size_t copy_amt, pg_off, pg_idx;
	volatile size_t so_far = 0;		/* volatile for waserror */
	const uint8_t *buf_end = buf + count;
	int error;

	qlock(&f->qlock);
	if (waserror()) {
		qunlock(&f->qlock);
		if (so_far) {
			poperror();
			return so_far;
		}
		nexterror();
	};
	if (offset + count > f->dir.length) {
		if (!f->ops->can_grow_to(f, offset + count))
			error(EINVAL, "can't write file to %lu bytes", offset + count);
	}
	f->flags |= FSF_DIRTY;
	while (buf < buf_end) {
		pg_off = PGOFF(offset + so_far);
		pg_idx = LA2PPN(offset + so_far);
		error = pm_load_page(f->pm, pg_idx, &page);
		if (error)
			error(-error, "write pm_load_page failed");
		copy_amt = MIN(PGSIZE - pg_off, buf_end - buf);
		memcpy_from_safe(page2kva(page) + pg_off, buf, copy_amt);
		buf += copy_amt;
		so_far += copy_amt;
		atomic_or(&page->pg_flags, PG_DIRTY);
		pm_put_page(page);
	}
	assert(buf == buf_end);
	/* We set the len *after* writing for our lockless reads.  If we set len
	 * before, then read() could start as soon as we loaded the page (all
	 * zeros), but before we wrote the actual data.  They'd get zeros instead of
	 * what we added. */
	if (offset + count > f->dir.length)
		WRITE_ONCE(f->dir.length, offset + count);
	__set_acmtime(f, FSF_MTIME | FSF_CTIME);
	qunlock(&f->qlock);
	poperror();
	return so_far;
}

static void wstat_mode(struct fs_file *f, int new_mode)
{
	ERRSTACK(1);
	int mode;

	qlock(&f->qlock);
	if (waserror()) {
		qunlock(&f->qlock);
		nexterror();
	}
	if (!caller_is_username(f->dir.uid))
		error(EPERM, "wrong user for wstat, need %s", f->dir.uid);
	/* Only allowing changes in permissions, not random stuff like whether it is
	 * a directory or symlink. */
	static_assert(!(DMMODE_BITS & S_PMASK));
	mode = (f->dir.mode & ~S_PMASK) | (new_mode & S_PMASK);
	WRITE_ONCE(f->dir.mode, mode);
	__set_acmtime(f, FSF_CTIME);
	qunlock(&f->qlock);
	poperror();
}

size_t fs_file_wstat(struct fs_file *f, uint8_t *m_buf, size_t m_buf_sz)
{
	struct dir *m_dir;
	size_t m_sz;

	/* common trick in wstats.  we want the dir and any strings in the M.  the
	 * strings are smaller than the entire M (which is strings plus the real dir
	 * M).  the strings will be placed right after the dir (dir[1]) */
	m_dir = kzmalloc(sizeof(struct dir) + m_buf_sz, MEM_WAIT);
	m_sz = convM2D(m_buf, m_buf_sz, &m_dir[0], (char*)&m_dir[1]);
	if (!m_sz) {
		kfree(m_dir);
		error(ENODATA, "couldn't convM2D");
	}
	/* We'll probably have similar issues for all of the strings.  At that
	 * point, we might not even bother reading the strings in. */
	if (!emptystr(m_dir->name))
		error(EINVAL, "do not rename with wstat");
	if (m_dir->mode != -1)
		wstat_mode(f, m_dir->mode);
	if (m_dir->length != -1)
		fs_file_truncate(f, m_dir->length);
	if ((int64_t)m_dir->atime.tv_sec != -1)
		set_acmtime_to(f, FSF_ATIME, &m_dir->atime);
	if ((int64_t)m_dir->btime.tv_sec != -1)
		set_acmtime_to(f, FSF_BTIME, &m_dir->btime);
	if ((int64_t)m_dir->ctime.tv_sec != -1)
		set_acmtime_to(f, FSF_CTIME, &m_dir->ctime);
	if ((int64_t)m_dir->mtime.tv_sec != -1)
		set_acmtime_to(f, FSF_MTIME, &m_dir->mtime);
	/* TODO: handle uid/gid/muid changes */
	kfree(m_dir);
	return m_sz;
}
