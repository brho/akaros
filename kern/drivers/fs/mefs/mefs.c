/* Copyright (c) 2018 Google Inc
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * #mefs: Memory Extent Filesystem
 *
 * It's designed to run on memory segments, supporting a small number of files
 * whose sizes are bimodal - either small, or potentially very large.  Small
 * files are O(PGSIZE).  Large files are O(TB).
 *
 * We're not designing for persistence in the face of failures, hardcore
 * performance, or anything like that.  I'd like it to be simple, yet capable of
 * handling very large files.
 *
 * There's only one instance of mefs, similar to KFS and unlike tmpfs.  All
 * attaches get the same FS.
 */

#include <ns.h>
#include <kmalloc.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <error.h>
#include <tree_file.h>
#include <pmap.h>

#include "mefs.h"

struct dev mefs_devtab;

struct mefs {
	struct tree_filesystem		tfs;
	struct mefs_superblock		*sb;



	atomic_t					qid;
};

static struct mefs mefs[1];

static uint64_t mefs_get_qid_path(struct mefs *mefs)
{
	return atomic_fetch_and_add(&mefs->qid, 1);
}

static char *devname(void)
{
	return mefs_devtab.name;
}

static void mefs_tf_free(struct tree_file *tf)
{
	/* We have nothing special hanging off the TF */
}

static void mefs_tf_unlink(struct tree_file *parent, struct tree_file *child)
{
	/* This is the "+1 for existing" ref. */
	tf_kref_put(child);
}

static void __mefs_tf_init(struct tree_file *tf, int dir_type, int dir_dev,
                            struct username *user, int perm)
{
	struct dir *dir = &tf->file.dir;

	fs_file_init_dir(&tf->file, dir_type, dir_dev, user, perm);
	dir->qid.path = mefs_get_qid_path((struct mefs*)tf->tfs);
	dir->qid.vers = 0;
	/* This is the "+1 for existing" ref.  There is no backing store for the FS,
	 * such as a disk or 9p, so we can't get rid of a file until it is unlinked
	 * and decreffed.  Note that KFS doesn't use pruners or anything else. */
	__kref_get(&tf->kref, 1);
}

/* Note: If your TFS doesn't support symlinks, you need to error out */
static void mefs_tf_create(struct tree_file *parent, struct tree_file *child,
                            int perm)
{
	__mefs_tf_init(child, parent->file.dir.type, parent->file.dir.dev, &eve,
	                perm);
}

static void mefs_tf_rename(struct tree_file *tf, struct tree_file *old_parent,
                            struct tree_file *new_parent, const char *name,
                            int flags)
{
	/* We don't have a backend, so we don't need to do anything additional for
	 * rename. */
}

static bool mefs_tf_has_children(struct tree_file *parent)
{
	/* The tree_file parent list is complete and not merely a cache for a real
	 * backend. */
	return !list_empty(&parent->children);
}

struct tree_file_ops mefs_tf_ops = {
	.free = mefs_tf_free,
	.unlink = mefs_tf_unlink,
	.lookup = NULL,
	.create = mefs_tf_create,
	.rename = mefs_tf_rename,
	.has_children = mefs_tf_has_children,
};

/* Fills page with its contents from its backing store file.  For KFS, that
 * means we're creating or extending a file, and the contents are 0.  Note the
 * page/offset might be beyond the current file length, based on the current
 * pagemap code. */
static int mefs_pm_readpage(struct page_map *pm, struct page *pg)
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
static int mefs_pm_writepage(struct page_map *pm, struct page *pg)
{
	return 0;
}

static void mefs_fs_punch_hole(struct fs_file *f, off64_t begin, off64_t end)
{
}

static bool mefs_fs_can_grow_to(struct fs_file *f, size_t len)
{
	/* TODO: implement some sort of memory limit */
	return true;
}

struct fs_file_ops mefs_fs_ops = {
	.readpage = mefs_pm_readpage,
	.writepage = mefs_pm_writepage,
	.punch_hole = mefs_fs_punch_hole,
	.can_grow_to = mefs_fs_can_grow_to,
};

static struct mefs *chan_to_mefs(struct chan *c)
{
	struct tree_file *tf = chan_to_tree_file(c);

	return (struct mefs*)(tf->tfs);
}

extern physaddr_t mefs_start;
extern size_t mefs_size;

static void mefs_init(void)
{
	ERRSTACK(1);
	struct tree_filesystem *tfs = (struct tree_filesystem*)mefs;
	struct mefs_superblock *sb;

	if (waserror()) {
		printk("#mefs threw %s\n", current_errstr());
		poperror();
		return;
	}
	if (!mefs_start)
		error(ENOENT, "Couldn't find mefs_start, aborting");
	sb = mefs_super_attach(mefs_start, mefs_size);
	if (sb) {
		printk("Found existing mefs sb at %p, reconnecting.\n", sb);
	} else {
		sb = mefs_super_create(mefs_start, mefs_size);
		printk("Created new mefs sb at %p\n", sb);

		mefs_ext_alloc(sb, PGSIZE << 0);
		mefs_ext_alloc(sb, PGSIZE << 0);
		void * x = mefs_ext_alloc(sb, PGSIZE << 10);
		mefs_ext_alloc(sb, PGSIZE << 5);
		mefs_ext_alloc(sb, PGSIZE << 1);
		mefs_ext_free(sb, x);
		mefs_ext_alloc(sb, PGSIZE << 7);
	}
	mefs_super_dump(sb);
	
	mefs->sb = sb;
// XXX

	
	/* This gives us one ref on root, which we'll never drop. */
	tfs_init(tfs);
	tfs->tf_ops = mefs_tf_ops;
	tfs->fs_ops = mefs_fs_ops;

	// XXX
	/* This gives us an extra refcnt on tfs->root.  This is "+1 for existing."
	 * It is decreffed during the purge CB. */
	__mefs_tf_init(tfs->root, &mefs_devtab - devtab, 0, &eve, DMDIR | 0777);
	poperror();
}

static struct chan *mefs_attach(char *spec)
{
	struct tree_filesystem *tfs = (struct tree_filesystem*)mefs;

	return tree_file_alloc_chan(tfs->root, &mefs_devtab, "#mefs");
}

static unsigned long mefs_chan_ctl(struct chan *c, int op, unsigned long a1,
                                    unsigned long a2, unsigned long a3,
                                    unsigned long a4)
{
	switch (op) {
	case CCTL_SYNC:
		return 0;
	default:
		error(EINVAL, "%s does not support %d", __func__, op);
	}
}

struct dev mefs_devtab __devtab = {
	.name = "mefs",
	.reset = devreset,
	.init = mefs_init,
	.shutdown = devshutdown,
	.attach = mefs_attach,
	.walk = tree_chan_walk,
	.stat = tree_chan_stat,
	.open = tree_chan_open,
	.create = tree_chan_create,
	.close = tree_chan_close,
	.read = tree_chan_read,
	.bread = devbread,
	.write = tree_chan_write,
	.bwrite = devbwrite,
	.remove = tree_chan_remove,
	.rename = tree_chan_rename,
	.wstat = tree_chan_wstat,
	.power = devpower,
	.chaninfo = devchaninfo,
	.mmap = tree_chan_mmap,
	.chan_ctl = mefs_chan_ctl,
};


// 	XXX
//
//	syslinux or something didn't work - the segment was zeroed.
//			might need a kexec
//				device teardown?  none of that shit was tested. (NICs)
//			k, it's a large ball.
//				need that ball to not be in the 'overwrite' spot
//				the new one defines the size of the overwrite spot too (elf
//				parse, etc)
//			need a chunk of code, running on its own protected page tables
//				need that to also not be in the overwrite spot
//			protected gdt too, and stack page.  can disable irqs...
//			memcpy to the final location, jump to it.
//				basically the elf parser, similar to loadelf.c
//				ah, but can't use any external code either.
//			maybe kexec is a super-slim OS
//				actually, we can bundle it with the target OS image.
//				set up its PT in advance?  
//					need to do it at runtime, since we need the paddr
//					
//			
//
//	will want to destroy the super aggressively.  or at least have commands for
//	it, so that if we e.g. barcher a new kernel, we're not stuck with the bugs
//
//	init is hokey.  would like to grow and shrink, and need to sync btw the base
//	arena, mefs, and whatever we do to communicate to our future self.
//		actually, mefs will describe itself
//		but the future self / multiboot memory detection is trickier
//		handing segments back is a little trickier (can make a yank function,
//		then arena add.  though that fragments the space slightly)
//
//
// don't forget some way to sync, if necessary (since we don't sync on unmount)
// 		btw, should unmount.c also sync?
//
//
//
// 	btw, for hole-punching, we might not be able to free the intermediate data
// 	easily.  would need to break it up.
// 		issue is that we don't have individual blocks - we have a large
// 		structure.  and the arena code won't take something that didn't have a
// 		btag
