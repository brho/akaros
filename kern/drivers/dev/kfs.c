/* Copyright (c) 2018 Google Inc
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * #kfs, in-memory ram filesystem, pulling from the kernel's embedded CPIO
 */

#include <ns.h>
#include <kmalloc.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <error.h>
#include <tree_file.h>
#include <pmap.h>
#include <cpio.h>

struct dev kfs_devtab;

struct kfs {
	struct tree_filesystem		tfs;
	atomic_t					qid;
} kfs;

static uint64_t kfs_get_qid_path(void)
{
	return atomic_fetch_and_add(&kfs.qid, 1);
}

static char *devname(void)
{
	return kfs_devtab.name;
}

static void kfs_tf_free(struct tree_file *tf)
{
	/* We have nothing special hanging off the TF */
}

static void kfs_tf_unlink(struct tree_file *parent, struct tree_file *child)
{
	/* This is the "+1 for existing" ref. */
	tf_kref_put(child);
}

static void __kfs_tf_init(struct tree_file *tf, int dir_type, int dir_dev,
                          struct username *user, int perm)
{
	struct dir *dir = &tf->file.dir;

	fs_file_init_dir(&tf->file, dir_type, dir_dev, user, perm);
	dir->qid.path = kfs_get_qid_path();
	dir->qid.vers = 0;
	/* This is the "+1 for existing" ref.  There is no backing store for the FS,
	 * such as a disk or 9p, so we can't get rid of a file until it is unlinked
	 * and decreffed.  Note that KFS doesn't use pruners or anything else. */
	__kref_get(&tf->kref, 1);
}

/* Note: If your TFS doesn't support symlinks, you need to error out */
static void kfs_tf_create(struct tree_file *parent, struct tree_file *child,
                          int perm)
{
	__kfs_tf_init(child, parent->file.dir.type, parent->file.dir.dev, &eve,
	              perm);
}

static void kfs_tf_rename(struct tree_file *tf, struct tree_file *old_parent,
                          struct tree_file *new_parent, const char *name,
                          int flags)
{
	/* We don't have a backend, so we don't need to do anything additional for
	 * rename. */
}

static bool kfs_tf_has_children(struct tree_file *parent)
{
	/* The tree_file parent list is complete and not merely a cache for a real
	 * backend. */
	return !list_empty(&parent->children);
}

struct tree_file_ops kfs_tf_ops = {
	.free = kfs_tf_free,
	.unlink = kfs_tf_unlink,
	.lookup = NULL,
	.create = kfs_tf_create,
	.rename = kfs_tf_rename,
	.has_children = kfs_tf_has_children,
};

/* Fills page with its contents from its backing store file.  For KFS, that
 * means we're creating or extending a file, and the contents are 0.  Note the
 * page/offset might be beyond the current file length, based on the current
 * pagemap code. */
static int kfs_pm_readpage(struct page_map *pm, struct page *pg)
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
static int kfs_pm_writepage(struct page_map *pm, struct page *pg)
{
	return 0;
}

static void kfs_fs_punch_hole(struct fs_file *f, off64_t begin, off64_t end)
{
}

static bool kfs_fs_can_grow_to(struct fs_file *f, size_t len)
{
	/* TODO: implement some sort of memory limit */
	return true;
}

struct fs_file_ops kfs_fs_ops = {
	.readpage = kfs_pm_readpage,
	.writepage = kfs_pm_writepage,
	.punch_hole = kfs_fs_punch_hole,
	.can_grow_to = kfs_fs_can_grow_to,
};

/* Consumes root's chan, even on error. */
static struct chan *__add_kfs_dir(struct chan *root, char *path,
                                  struct cpio_bin_hdr *c_bhdr)
{
	ERRSTACK(1);
	struct chan *c;

	if (waserror()) {
		warn("failed to add %s", path);
		cclose(root);
		poperror();
		return NULL;
	}
	c = namec_from(root, path, Acreate, O_EXCL, DMDIR | c_bhdr->c_mode, NULL);
	poperror();
	return c;
}

static struct chan *__add_kfs_symlink(struct chan *root, char *path,
                                      struct cpio_bin_hdr *c_bhdr)
{
	ERRSTACK(1);
	struct chan *c;
	char target[c_bhdr->c_filesize + 1];

	if (waserror()) {
		warn("failed to add %s", path);
		cclose(root);
		poperror();
		return NULL;
	}
	strncpy(target, c_bhdr->c_filestart, c_bhdr->c_filesize);
	target[c_bhdr->c_filesize] = 0;
	c = namec_from(root, path, Acreate, O_EXCL,
	               DMSYMLINK | S_IRWXU | S_IRWXG | S_IRWXO, target);
	poperror();
	return c;
}

static struct chan *__add_kfs_file(struct chan *root, char *path,
                                   struct cpio_bin_hdr *c_bhdr)
{
	ERRSTACK(1);
	struct chan *c;
	off64_t offset = 0;
	size_t ret, amt = c_bhdr->c_filesize;
	void *buf = c_bhdr->c_filestart;

	if (waserror()) {
		warn("failed to add %s", path);
		cclose(root);
		poperror();
		return NULL;
	}
	c = namec_from(root, path, Acreate, O_EXCL | O_RDWR, c_bhdr->c_mode, NULL);
	poperror();
	if (waserror()) {
		warn("failed to modify %s", path);
		cclose(c);
		poperror();
		return NULL;
	}
	while (amt) {
		ret = devtab[c->type].write(c, buf + offset, amt, offset);
		amt -= ret;
		offset += ret;
	}
	poperror();
	return c;
}

static int add_kfs_entry(struct cpio_bin_hdr *c_bhdr, void *cb_arg)
{
	struct tree_file *root = cb_arg;
	char *path = c_bhdr->c_filename;
	struct chan *c;
	struct tree_file *tf;
	struct timespec ts;

	/* Root of the FS, already part of KFS */
	if (!strcmp(path, "."))
		return 0;
	c = tree_file_alloc_chan(root, &kfs_devtab, "#kfs");
	switch (c_bhdr->c_mode & CPIO_FILE_MASK) {
	case (CPIO_DIRECTORY):
		c = __add_kfs_dir(c, path, c_bhdr);
		break;
	case (CPIO_SYMLINK):
		c = __add_kfs_symlink(c, path, c_bhdr);
		break;
	case (CPIO_REG_FILE):
		c = __add_kfs_file(c, path, c_bhdr);
		break;
	default:
		cclose(c);
		warn("Unknown file type %d in the CPIO!",
		     c_bhdr->c_mode & CPIO_FILE_MASK);
		return -1;
	}
	if (!c)
		return -1;
	tf = chan_to_tree_file(c);
	ts.tv_sec = c_bhdr->c_mtime;
	ts.tv_nsec = 0;
	/* Lockless */
	__set_acmtime_to(&tf->file, FSF_ATIME | FSF_BTIME | FSF_CTIME | FSF_MTIME,
	                 &ts);
	/* TODO: consider UID/GID.  Right now, everything is owned by eve. */
	cclose(c);
	return 0;
}

struct cpio_info {
	void *base;
	size_t sz;
};

static void kfs_get_cpio_info(struct cpio_info *ci)
{
	extern uint8_t _binary_obj_kern_initramfs_cpio_size[];
	extern uint8_t _binary_obj_kern_initramfs_cpio_start[];

	ci->base = (void*)_binary_obj_kern_initramfs_cpio_start;
	ci->sz = (size_t)_binary_obj_kern_initramfs_cpio_size;
}

static void kfs_extract_cpio(struct cpio_info *ci)
{
	parse_cpio_entries(ci->base, ci->sz, add_kfs_entry, kfs.tfs.root);
}

static void kfs_free_cpio(struct cpio_info *ci)
{
	void *base = ci->base;
	size_t sz = ci->sz;

	/* The base arena requires page aligned, page sized segments. */
	sz -= ROUNDUP(base, PGSIZE) - base;
	sz = ROUNDDOWN(sz, PGSIZE);
	base = ROUNDUP(base, PGSIZE);
	/* Careful - the CPIO is part of the kernel blob and a code address. */
	base = KBASEADDR(base);
	printk("Freeing %d MB of CPIO RAM\n", sz >> 20);
	arena_add(base_arena, base, sz, MEM_WAIT);
}

static void kfs_init(void)
{
	struct tree_filesystem *tfs = &kfs.tfs;
	struct cpio_info ci[1];

	/* This gives us one ref on tfs->root. */
	tfs_init(tfs);
	tfs->tf_ops = kfs_tf_ops;
	tfs->fs_ops = kfs_fs_ops;
	/* Note this gives us the "+1 for existing" ref on tfs->root. */
	__kfs_tf_init(tfs->root, &kfs_devtab - devtab, 0, &eve, DMDIR | 0777);
	/* Other devices might want to create things like kthreads that run the LRU
	 * pruner or PM sweeper. */
	kfs_get_cpio_info(ci);
	kfs_extract_cpio(ci);
	kfs_free_cpio(ci);
	/* This has another kref.  Note that each attach gets a ref and each new
	 * process gets a ref. */
	kern_slash = tree_file_alloc_chan(kfs.tfs.root, &kfs_devtab, "/");
}

static struct chan *kfs_attach(char *spec)
{
	/* The root TF has a new kref for the attach chan */
	return tree_file_alloc_chan(kfs.tfs.root, &kfs_devtab, "#kfs");
}

static unsigned long kfs_chan_ctl(struct chan *c, int op, unsigned long a1,
                                  unsigned long a2, unsigned long a3,
                                  unsigned long a4)
{
	switch (op) {
	case CCTL_SYNC:
		return 0;
	default:
		return tree_chan_ctl(c, op, a1, a2, a3, a4);
	}
}

struct dev kfs_devtab __devtab = {
	.name = "kfs",
	.reset = devreset,
	.init = kfs_init,
	.shutdown = devshutdown,
	.attach = kfs_attach,
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
	.chan_ctl = kfs_chan_ctl,
};
