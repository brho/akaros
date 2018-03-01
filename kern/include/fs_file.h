/* Copyright (c) 2018 Google Inc
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * fs_file: structs and helpers for files for 9ns devices
 */

#pragma once

#include <ns.h>
#include <pagemap.h>

/* fs_file has all the info of the 9ns dir and the dirtab (which is a subset of
 * struct dir), plus whatever is needed for generic mmappable filesystems.
 * Specifically, it has a page_map and a few synchronization fields.  It doesn't
 * need to support mmap either.  Longer range, fs_file can replace dirtab for
 * static device filesystems.
 *
 * Most fs_files will end up being tree_files (see below).  Some devices might
 * use fs_files that are hooked in to their own tree structures.
 *
 * fs_info is for devices to use.  For instance, #mnt will probably have an
 * actual chan for every walked fs_file.  It will use that chan/FID for all of
 * its operations on the backend: walking to discover child files, stat()ing to
 * get fs_file metadata, reading and writing pages for the page cache, etc.
 *
 *
 * Synchronization rules
 * ------------------
 * Note this qlock is also used by tree_file code.  See below for details.
 *
 * Hold the qlock when changing the contents of a file.  This includes writes
 * (which need to adjust the len and add data), truncate / hole-punches
 * (which need to adjust the len and remove data), changing parts of dir (name,
 * permissions/mode, atimes), etc.  Tree files will hold this qlock when
 * changing a parent's children.
 *
 * Readers who just 'peak' at atomically-writable fields do not need to grab the
 * qlock.  For example, you can glance at dir->perms.  In arbitrary cases, you
 * can't look at name, since we can't change it atomically (it can smear or run
 * into other issues).  'name' is somewhat special: if you find the tree_file
 * via a RCU protected wc hash lookup, you can access name.
 *
 * fs_file_read() can skip the qlock in the common case.  It can try to grab a
 * page from the PM (if it was there, then some ordering exists on the
 * concurrent ops where the read partially succeeds).  If the read fails to find
 * a page, then we'd need to lock, since that interacts with length.
 *
 * There's actually not a lot in dir that is atomically writable.
 * atime/mtime/ctime won't be when we use timespecs.  We might be able to craft
 * something for lockless reads of dir state with seq counters.  That would save
 * a qlock for each fs_file on a readdir.  It's a little tricky; we probably
 * need to kfree_rcu the dir->name (never leak in a convD2M, even if we retry),
 * and we need to be careful about multi-field changes where the changer might
 * block.  i.e. don't hold a seq lock when making a backend op, which could
 * block.  Not too concerned with this for now, esp since it's not clear how
 * chown will work.
 *
 * Note that even if we can atomically write to the dir, we should still grab
 * the qlock.  The reason is that there may be readers who want to make sure
 * parts of the dir doesn't change.
 *
 * The PM code is pretty rough.  For now, we're using the old VFS PM code, but
 * it needs a few changes:
 * - Hole punching / truncate:  pm_remove_contig() leaves behind pages if they
 *   were concurrently opened.  We can't detach from the tree since the pm_page
 *   assumes it is always hooked in.  This means a truncate during a read or
 *   mmap could leave data behind in the PM, which will be accessible when len
 *   gets increased again (e.g. seek and write).  Truncate could zero that page,
 *   though we'd need a history counter so reads don't see a partial value.
 *   Further, pinned VMRs get skipped.  During trunc, they should get removed
 *   and generate a SIGBUS on access.
 * - Heavily integrated with struct page.  Maybe some sort of 'struct page' on
 *   demand, that is built for an IO mapping.  There might be some huge-page
 *   stuff here too.
 * - The pm_ops callbacks and whatnot could use some help, even just the
 *   arguments and indirections.  Maybe integrate them more into the funcs, e.g.
 *   fs_file_*().  They probably should be fs_file_ops.  For now, the pm ops
 *   assume the qlock is held.
 */

struct fs_file;

/* TODO: Once we get rid of the VFS and rework the PM, we can put the PM ops in
 * here properly. */
struct fs_file_ops {
	struct page_map_operations;	/* readpage and writepage */
	void (*punch_hole)(struct fs_file *f, off64_t begin, off64_t end);
	bool (*can_grow_to)(struct fs_file *f, size_t len);
};

#define FSF_DIRTY				(1 << 1)

struct fs_file {
	struct dir					dir;
	int							flags;
	qlock_t						qlock;
	struct fs_file_ops			*ops;
	struct page_map				*pm;
	void						*priv;

	/* optional inline storage */
	char						static_name[KNAMELEN];	/* for dir->name */
	struct page_map				static_pm;				/* for pm */
	/* we need to be aligned to 64 bytes for the linker tables. */
} __attribute__ ((aligned(64)));

static inline bool caller_has_file_perms(struct fs_file *f, int omode)
{
	return caller_has_dir_perms(&f->dir, omode);
}

static inline void fs_file_perm_check(struct fs_file *f, int omode)
{
	dir_perm_check(&f->dir, omode);
}

void fs_file_init(struct fs_file *f, const char *name, struct fs_file_ops *ops);
void fs_file_set_basename(struct fs_file *f, const char *name);
void fs_file_change_basename(struct fs_file *f, const char *name);
void fs_file_init_dir(struct fs_file *f, int dir_type, int dir_dev,
                      struct username *user, int perm);
void fs_file_copy_from_dir(struct fs_file *f, struct dir *dir);
void cleanup_fs_file(struct fs_file *f);

#define FSF_ATIME				(1 << 0)
#define FSF_BTIME				(1 << 1)
#define FSF_CTIME				(1 << 2)
#define FSF_MTIME				(1 << 3)

void __set_acmtime_to(struct fs_file *f, int which, struct timespec *t);
void __set_acmtime(struct fs_file *f, int which);
void set_acmtime_to(struct fs_file *f, int which, struct timespec *t);
void set_acmtime_noperm(struct fs_file *f, int which);

size_t fs_file_stat(struct fs_file *f, uint8_t *m_buf, size_t m_buf_sz);
void fs_file_truncate(struct fs_file *f, off64_t to);
size_t fs_file_read(struct fs_file *f, uint8_t *buf, size_t count,
                    off64_t offset);
size_t fs_file_write(struct fs_file *f, const uint8_t *buf, size_t count,
                     off64_t offset);
size_t fs_file_wstat(struct fs_file *f, uint8_t *m_buf, size_t m_buf_sz);
