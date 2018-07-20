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
		error(EINVAL, "%s does not support %d", __func__, op);
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


// XXX misc TODO
// --------------------------------------------------
// bash doesn't give us errstr...
// e.g. 
// 		bash-4.3$ echo ffff  >> /prog/goo
// 		bash: /prog/goo: Operation not permitted
// 		bash-4.3$ ash
// 		/ $ echo ffff >> /prog/goo
// 		ash: can't create /prog/goo: devpermcheck(goo, 0644, 03102) failed
// 			that's a little weird.  it was already created...  could be an ash
// 			thing
// 		/ $ write_to /prog/goo fff
// 		Can't open path: Operation not permitted, devpermcheck(goo, 0644, 03)
// 		failed
//
// 		a little better.
// 		why are the perms fucked?  that was umask, and the owner is eve, but our
// 		username is nanwan or something.  maybe nothing.  but not eve.
// 		need umask 0002 or just 0, so we don't make a file 644 that we can't
// 		write
//
// bash when tabbing out cd, shows us all files, not just directories.
// 		not ash.  they do the readdir, then stat everything
// 		some difference with stat, they can't tell it's (not) a dir?
// 		not sure - bash does the readdir, but doesn't do the stat right away.
// 		the function it is in (rl_filename_completion_function) doesn't seem to
// 		care about directories vs files.  maybe it's not getting the right comp
// 		code?  bash does do a stat, but only after printing the name
// 		rmdir doesn't do it either.  also doesn't do it on busybox.
//
//
//  our linux list.h could use some safety shit, like WRITE_ONCE.  update to the
//  most recent one, perhaps?
//
// hashing
// - consider storing the hash in the tf.  might only be done twice
// - might be harder to resize, esp with RCU readers.  might need a seq.
// - consider hashing on the parent QID too.
// - consider bucket locks
// - consider exclusivity checks on insert (or caller's responsibility)
//
// ns devtab func signature issue
// 		qbwrite and whatnot is ssize_t, and some cases can return -1.
// 			who calls that?
// 			how do we call devtab.bread (e.g. pipe)
// 			these funcs don't always throw
// 			ipbwrite just says it wrote it all.
// 		prob should review the functions like pipebread 
//
// 		convD2M is unsigned int
//
// 		netifbread/bwrite/read/write
//
// have waserror() check for irq/trap depth
//
//
// XXX bigger shit
//
//
//   how do we trigger the shrink of the cache?  (memory pressure)
//   	- need to talk to the instance, e.g. versions of gtfs/tmpfs
//   	- walking the NS to find those chans is hard
//   	- having a CB where they register with the memory system might be better
//   		
// 	maybe related: some sort of chan op that returns an FD for a ctl FS
// 		imagine you want to get access to a control plane for a mounted
// 		device, such as a #gtfs.  you want to fuck with various settings.
//
// 		how do you attach this?
// 			it probably doesn't speak 9p, so it'd be a bind
// 			but sort of like mnt, we had a path to a chan, then did chanctl,
// 			then the result of that is bound
// 			- we need something to attach.  that chan_ctl can return an
// 			attachable chan to something else within the device?
// 				but then every op e.g. gtfs_write would need to know if it was
// 				talking to the real thing or something else
// 		maybe it'd be better to have an 'introspection' device, a different
// 		#peek or something.
// 			- this device takes a chan, like mount, as arguments for its attach
// 			and it has a small set of kobj/sysfs like ops that the peekee
// 			implements
// 			- just a device that knows about another device and can have custom
// 			hooks/commands/etc
// 			- though this might not work as well with 9p.  issue is the
// 			interface between devices - if it's not 9p/devtab, then we're
// 			somewhat screwed
// 		say we had a chan flag, with tainting, e.g. CTL_PLANE or something
// 			we'll still never be able to have a device that supports this just
// 			have e.g. tree_chan_walk as its method.  everything gets a layer.
//
//
// 	btw, chan_ctl's numbers are currently independent of fcntls, and there is no
// 	way to talk directly to chan_ctl (just like you can't call dev.create).  not
// 	a problem yet, but if we want arbitrary chan_ctl, then we might change the
// 	numbers
// 		for instance, if i wanted to add a hokey chan_ctl for gtfs memory
// 		writeback or debugging.  i can't access that from userspace.  hence
// 		kfunc
// 		rel to the #peek device, chan_ctl might be the source for some blob
// 		pointer / hook.  if userspace provides an FD, like mnt, then we'd need a
// 		way to get it.
// 			and the numbers for that are the CCTL_X, which are e.g. F_SETFL
// 			maybe.  or maybe we have interposition layers, esp since F_GETFD is
// 			about the FD, not the chan.
//
//
// 	want a gtfs ktask that syncs or LRU frees on occasion?
//
// 	glibc uses the old convD2M and friends (also grep STATFIXLEN)
//
// 	RCU
// 		^^^^^^^^^
//
// 	better mmap infrastructure
// 			get rid of file_or_chan once we sort this out.  right now, it has
// 			both chan and fs_file
//
// 	mmap notes
//
// 		newer note:
// 			we have foc_dev_mmap, but that doesn't pm_add_vmr.
// 				it could, but we also have pm_add_vmr when duplicating etc
//		 		maybe that dev_mmap op ought to do both the pm_add_vmr and remove.
//		 		call the op on both ends
//		 		we'll need a counter for the number of dirtiable VMRs
//
//		 also, consider nesting / layering devices, even through the TFS.
//		 we might want to pass through to the block device/backend if it has an
//		 mmap op, since that could tell us the page-ish struct to use
//
//		when we talk to 9ns, we want to handle things other than PMs.  like
//		arbitrary physical memory
//			optional callback for unmapping (i.e. when the device wants to
//			revoke the VMR connection, e.g. PM flusher)
//
//			instead of PM, maybe something a little higher
//				like the fs_file
//				or the PM itself points to those pages.  not quite a PM, in that
//				it doesn't allocate pages.  we just want to know where to point.
//
// 				tempted to have __vm_foc be an fs_file, though sometimes we need
// 				its absolute path (perf), which is a chan feature.
//
//		what's the connection from VMR to file and from PM back to VMR?
//			IIRC, PM has weak refs on the VMRs, VMRs have refs on file -> PM
//			VMRs have refs on files/chan: the mmap lasts beyond the FD closing
//				though it might not need to be the chan.  could be fs_file
//				depends on what the interface is - everything with chans and
//				whatnot, multiplexed through a devtab[c->type].mmap op.
//					9p mmap op? probably not
//						say you want to access a NIC on another machine
//						9p mnt - can you do that?  it'll fake it with a mmap on
//						the frontend, implemented with reads to the backend
//
//  fs_file is doing some nasty things with usernames.  everyone is eve,
//  basically, and there's no real accounting.
//  	could change e.g. dir->uid to refcnts on struct user
//  		refcnting is a bit nasty, want something like 'users never go away'
//  	also need to interpet 9p's usernames. 
//  		like lookup, given name, hook in
//  		need something for unknown users.  eve?  mount owner?
//  	also, sort out any other rules for the dir->strings.  e.g. ext can be 0
//
//	missing chown
//		that, and glibc set errno, but has an old errstr
//			bash-4.3$ mv /prog/file /prog/f2
//			mv: can't preserve ownership of '/prog/f2': Function not implemented, could not find name f2, dev root
//		
//
// 	XXX VM shit
// 		can we move all the PG_ flags out of struct page?
// 			we have PG_REMOVAL and PM_REMOVAL.  ffs.
// 				PG_REMOVAL is used to communicate through the mem_walk
// 				callbacks
// 				PG_DIRTY is the response from the CB for a particular
// 				page too.  so it's bidirectional
// 			there's a giant sem in there too, for load_page
// 			can we have the radix point to something other than a page?
// 			like some on-demand struct that has all the flags
// 				we'll need a way for vmr_for_each to communicate back to
// 				us.
// 			do we want a pml walk?  slightly better than a
// 			foreach-pte_walk, since we don't have to go up and down.
// 			but the downside is we don't know the radix slot / PM info
// 			for a specific PTE.
// 				is there something we could pass that they can quickly
// 				find it? (rewalking the radix isn't 'quickly').  if so,
// 				we'd just do another PTE
//
// 				seems like we have two structures that are both radix
// 				trees: PMLs and pm_tree.  would be nice to merge.  can
// 				we walk them in sync?  or use the same one? 
// 					no to most, since a proc's KPT has many unrelated VMRs
//
// 				also, munmap is making a pass to mark not present
// 				anyways. (in regards to the for-each-pte-walk shit)
//
// 		maybe make all VMRs point to a "PM", even anon ones, instead of using
// 		the PTEs to track pages. 
// 			- then replace all of it with the radixvm trees
// 			- and this thing can track whatever paddrs we're pointing to
// 			- PTEs become weak refs, unlike the weird shit mm does now
// 			- fs files or pms?  (separate issues)
// 			- and to some extent, all of anon mem is really one giant PM, not N
// 			separate ones, and the VMRs are windows into that PM.
// 			- revisit locking the 'fs_file' and len check.  anon won't have len.
//
//
// side note: whenever we free pages, they stay in the slab layers, so it's hard
// to tell we're actually freeing them

	// 	XXX
	//
	// 		install this, maybe (requires sqlite3)
	// 		https://github.com/juntaki/gogtags
