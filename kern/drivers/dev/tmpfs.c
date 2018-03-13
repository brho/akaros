/* Copyright (c) 2018 Google Inc
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * #tmpfs, in-memory ram filesystem instance.
 *
 * Compared to #kfs, there can be many #tmpfs instances, all of which are
 * independent and clean themeselves up when they are detached and the last chan
 * is closed.  A few notes:
 * - The FS root ("/") can't be removed.  When you unmount, it gets
 *   closed/decreffed.
 * - The tmpfs will exist for the life of any open chans, including the mount.
 *   You can e.g. cd into a subdir, unmount /, and the tmpfs will stay alive til
 *   you leave the directory (basically close the chan).  You can do the same
 *   thing with open_at().
 * - The root tree_file will not be deleted so long as you have an open chan.
 *   Any open chan on a subdir/subfile will hold refs on the root.  The mount
 *   point will also hold those refs.  We also hold an additional +1 on the root
 *   TF, which we drop once we have no users and we've purged the tree. */

#include <ns.h>
#include <kmalloc.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <error.h>
#include <tree_file.h>
#include <pmap.h>
#include <cpio.h>

struct dev tmpfs_devtab;

struct tmpfs {
	struct tree_filesystem		tfs;
	atomic_t					qid;
	struct kref					users;
};

static uint64_t tmpfs_get_qid_path(struct tmpfs *tmpfs)
{
	return atomic_fetch_and_add(&tmpfs->qid, 1);
}

static char *devname(void)
{
	return tmpfs_devtab.name;
}

static void tmpfs_tf_free(struct tree_file *tf)
{
	/* We have nothing special hanging off the TF */
}

static void tmpfs_tf_unlink(struct tree_file *parent, struct tree_file *child)
{
	/* This is the "+1 for existing" ref. */
	tf_kref_put(child);
}

static void __tmpfs_tf_init(struct tree_file *tf, int dir_type, int dir_dev,
                            struct username *user, int perm)
{
	struct dir *dir = &tf->file.dir;

	fs_file_init_dir(&tf->file, dir_type, dir_dev, user, perm);
	dir->qid.path = tmpfs_get_qid_path((struct tmpfs*)tf->tfs);
	dir->qid.vers = 0;
	/* This is the "+1 for existing" ref.  There is no backing store for the FS,
	 * such as a disk or 9p, so we can't get rid of a file until it is unlinked
	 * and decreffed.  Note that KFS doesn't use pruners or anything else. */
	__kref_get(&tf->kref, 1);
}

/* Note: If your TFS doesn't support symlinks, you need to error out */
static void tmpfs_tf_create(struct tree_file *parent, struct tree_file *child,
                            int perm)
{
	__tmpfs_tf_init(child, parent->file.dir.type, parent->file.dir.dev, &eve,
	                perm);
}

static void tmpfs_tf_rename(struct tree_file *tf, struct tree_file *old_parent,
                            struct tree_file *new_parent, const char *name,
                            int flags)
{
	/* We don't have a backend, so we don't need to do anything additional for
	 * rename. */
}

static bool tmpfs_tf_has_children(struct tree_file *parent)
{
	/* The tree_file parent list is complete and not merely a cache for a real
	 * backend. */
	return !list_empty(&parent->children);
}

struct tree_file_ops tmpfs_tf_ops = {
	.free = tmpfs_tf_free,
	.unlink = tmpfs_tf_unlink,
	.lookup = NULL,
	.create = tmpfs_tf_create,
	.rename = tmpfs_tf_rename,
	.has_children = tmpfs_tf_has_children,
};

/* Fills page with its contents from its backing store file.  For KFS, that
 * means we're creating or extending a file, and the contents are 0.  Note the
 * page/offset might be beyond the current file length, based on the current
 * pagemap code. */
static int tmpfs_pm_readpage(struct page_map *pm, struct page *pg)
{
	memset(page2kva(pg), 0, PGSIZE);
	atomic_or(&pg->pg_flags, PG_UPTODATE);
	/* Pretend that we blocked while filing this page.  This catches a lot of
	 * bugs.  It does slightly slow down the kernel, but it's only when filling
	 * the page cache, and considering we are using a RAMFS, you shouldn't
	 * measure things that actually rely on KFS's performance. */
	kthread_usleep(1);
	return 0;
}

/* Meant to take the page from PM and flush to backing store.  There is no
 * backing store. */
static int tmpfs_pm_writepage(struct page_map *pm, struct page *pg)
{
	return 0;
}

static void tmpfs_fs_punch_hole(struct fs_file *f, off64_t begin, off64_t end)
{
}

static bool tmpfs_fs_can_grow_to(struct fs_file *f, size_t len)
{
	/* TODO: implement some sort of memory limit */
	return true;
}

struct fs_file_ops tmpfs_fs_ops = {
	.readpage = tmpfs_pm_readpage,
	.writepage = tmpfs_pm_writepage,
	.punch_hole = tmpfs_fs_punch_hole,
	.can_grow_to = tmpfs_fs_can_grow_to,
};

static void purge_cb(struct tree_file *tf)
{
	/* this is the +1 for existing */
	tf_kref_put(tf);
}

static void tmpfs_release(struct kref *kref)
{
	struct tmpfs *tmpfs = container_of(kref, struct tmpfs, users);

	tfs_frontend_purge(&tmpfs->tfs, purge_cb);
	/* this is the ref from attach */
	assert(kref_refcnt(&tmpfs->tfs.root->kref) == 1);
	tf_kref_put(tmpfs->tfs.root);
	/* ensures __tf_free() happens before tfs_destroy */
	rcu_barrier();
	tfs_destroy(&tmpfs->tfs);
	kfree(tmpfs);
}

static struct tmpfs *chan_to_tmpfs(struct chan *c)
{
	struct tree_file *tf = chan_to_tree_file(c);

	return (struct tmpfs*)(tf->tfs);
}

static void incref_tmpfs_chan(struct chan *c)
{
	kref_get(&chan_to_tmpfs(c)->users, 1);
}

static void decref_tmpfs_chan(struct chan *c)
{
	kref_put(&chan_to_tmpfs(c)->users);
}

static struct chan *tmpfs_attach(char *spec)
{
	struct tree_filesystem *tfs = kzmalloc(sizeof(struct tmpfs), MEM_WAIT);
	struct tmpfs *tmpfs = (struct tmpfs*)tfs;

	/* All distinct chans get a ref on the filesystem, so that we can destroy it
	 * when the last user disconnects/closes. */
	kref_init(&tmpfs->users, tmpfs_release, 1);

	/* This gives us one ref on root, dropped during tmpfs_release(). */
	tfs_init(tfs);
	tfs->tf_ops = tmpfs_tf_ops;
	tfs->fs_ops = tmpfs_fs_ops;

	/* This gives us an extra refcnt on tfs->root.  This is "+1 for existing."
	 * It is decreffed during the purge CB. */
	__tmpfs_tf_init(tfs->root, &tmpfs_devtab - devtab, 0, &eve, DMDIR | 0777);
	/* This also increfs, copying tfs->root's ref for the chan it returns. */
	return tree_file_alloc_chan(tfs->root, &tmpfs_devtab, "#tmpfs");
}

static struct walkqid *tmpfs_walk(struct chan *c, struct chan *nc, char **name,
                                  unsigned int nname)
{
	struct walkqid *wq = tree_chan_walk(c, nc, name, nname);

	if (wq && wq->clone && (wq->clone != c))
		incref_tmpfs_chan(wq->clone);
	return wq;
}

static void tmpfs_close(struct chan *c)
{
	tree_chan_close(c);
	decref_tmpfs_chan(c);
}

static void tmpfs_remove(struct chan *c)
{
	ERRSTACK(1);
	struct tmpfs *tmpfs = chan_to_tmpfs(c);

	/* This is a bit of a pain - when remove fails, we won't get a chance to
	 * close the chan.  See notes in tree_chan_remove() and sysremove(). */
	if (waserror()) {
		kref_put(&tmpfs->users);
		nexterror();
	}
	tree_chan_remove(c);
	kref_put(&tmpfs->users);
	poperror();
}

struct dev tmpfs_devtab __devtab = {
	.name = "tmpfs",
	.reset = devreset,
	.init = devinit,
	.shutdown = devshutdown,
	.attach = tmpfs_attach,
	.walk = tmpfs_walk,
	.stat = tree_chan_stat,
	.open = tree_chan_open,
	.create = tree_chan_create,
	.close = tmpfs_close,
	.read = tree_chan_read,
	.bread = devbread,
	.write = tree_chan_write,
	.bwrite = devbwrite,
	.remove = tmpfs_remove,
	.wstat = tree_chan_wstat,
	.power = devpower,
	.chaninfo = devchaninfo,
	.mmap = tree_chan_mmap,
};
