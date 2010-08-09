/* Copyright (c) 2009, 2010 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Default implementations and global values for the VFS. */

#include <vfs.h> // keep this first
#include <sys/queue.h>
#include <assert.h>
#include <stdio.h>
#include <atomic.h>
#include <slab.h>
#include <kmalloc.h>
#include <kfs.h>
#include <pmap.h>
#include <umem.h>

struct sb_tailq super_blocks = TAILQ_HEAD_INITIALIZER(super_blocks);
spinlock_t super_blocks_lock = SPINLOCK_INITIALIZER;
struct fs_type_tailq file_systems = TAILQ_HEAD_INITIALIZER(file_systems);
struct namespace default_ns;
// TODO: temp dcache, holds all dentries ever for now
struct dentry_slist dcache = SLIST_HEAD_INITIALIZER(dcache);
spinlock_t dcache_lock = SPINLOCK_INITIALIZER;

struct kmem_cache *dentry_kcache; // not to be confused with the dcache
struct kmem_cache *inode_kcache;
struct kmem_cache *file_kcache;

/* Mounts fs from dev_name at mnt_pt in namespace ns.  There could be no mnt_pt,
 * such as with the root of (the default) namespace.  Not sure how it would work
 * with multiple namespaces on the same FS yet.  Note if you mount the same FS
 * multiple times, you only have one FS still (and one SB).  If we ever support
 * that... */
struct vfsmount *mount_fs(struct fs_type *fs, char *dev_name,
                          struct dentry *mnt_pt, int flags,
                          struct namespace *ns)
{
	struct super_block *sb;
	struct vfsmount *vmnt = kmalloc(sizeof(struct vfsmount), 0);

	/* this first ref is stored in the NS tailq below */
	kref_init(&vmnt->mnt_kref, fake_release, 1);
	/* Build the vfsmount, if there is no mnt_pt, mnt is the root vfsmount (for now).
	 * fields related to the actual FS, like the sb and the mnt_root are set in
	 * the fs-specific get_sb() call. */
	if (!mnt_pt) {
		vmnt->mnt_parent = NULL;
		vmnt->mnt_mountpoint = NULL;
	} else { /* common case, but won't be tested til we try to mount another FS */
		mnt_pt->d_mount_point = TRUE;
		mnt_pt->d_mounted_fs = vmnt;
		kref_get(&vmnt->mnt_kref, 1); /* held by mnt_pt */
		vmnt->mnt_parent = mnt_pt->d_sb->s_mount;
		vmnt->mnt_mountpoint = mnt_pt;
	}
	TAILQ_INIT(&vmnt->mnt_child_mounts);
	vmnt->mnt_flags = flags;
	vmnt->mnt_devname = dev_name;
	vmnt->mnt_namespace = ns;
	kref_get(&ns->kref, 1); /* held by vmnt */

	/* Read in / create the SB */
	sb = fs->get_sb(fs, flags, dev_name, vmnt);
	if (!sb)
		panic("You're FS sucks");

	/* TODO: consider moving this into get_sb or something, in case the SB
	 * already exists (mounting again) (if we support that) */
	spin_lock(&super_blocks_lock);
	TAILQ_INSERT_TAIL(&super_blocks, sb, s_list); /* storing a ref here... */
	spin_unlock(&super_blocks_lock);

	/* Update holding NS */
	spin_lock(&ns->lock);
	TAILQ_INSERT_TAIL(&ns->vfsmounts, vmnt, mnt_list);
	spin_unlock(&ns->lock);
	/* note to self: so, right after this point, the NS points to the root FS
	 * mount (we return the mnt, which gets assigned), the root mnt has a dentry
	 * for /, backed by an inode, with a SB prepped and in memory. */
	return vmnt;
}

void vfs_init(void)
{
	struct fs_type *fs;

	dentry_kcache = kmem_cache_create("dentry", sizeof(struct dentry),
	                                  __alignof__(struct dentry), 0, 0, 0);
	inode_kcache = kmem_cache_create("inode", sizeof(struct inode),
	                                 __alignof__(struct inode), 0, 0, 0);
	file_kcache = kmem_cache_create("file", sizeof(struct file),
	                                __alignof__(struct file), 0, 0, 0);
	/* default NS never dies, +1 to exist */
	kref_init(&default_ns.kref, fake_release, 1);
	spinlock_init(&default_ns.lock);
	default_ns.root = NULL;
	TAILQ_INIT(&default_ns.vfsmounts);

	/* build list of all FS's in the system.  put yours here.  if this is ever
	 * done on the fly, we'll need to lock. */
	TAILQ_INSERT_TAIL(&file_systems, &kfs_fs_type, list);
	TAILQ_FOREACH(fs, &file_systems, list)
		printk("Supports the %s Filesystem\n", fs->name);

	/* mounting KFS at the root (/), pending root= parameters */
	// TODO: linux creates a temp root_fs, then mounts the real root onto that
	default_ns.root = mount_fs(&kfs_fs_type, "RAM", NULL, 0, &default_ns);

	printk("vfs_init() completed\n");
}

/* Builds / populates the qstr of a dentry based on its d_iname.  If there is an
 * l_name, (long), it will use that instead of the inline name.  This will
 * probably change a bit. */
void qstr_builder(struct dentry *dentry, char *l_name)
{
	dentry->d_name.name = l_name ? l_name : dentry->d_iname;
	// TODO: pending what we actually do in d_hash
	//dentry->d_name.hash = dentry->d_op->d_hash(dentry, &dentry->d_name); 
	dentry->d_name.hash = 0xcafebabe;
	dentry->d_name.len = strnlen(dentry->d_name.name, MAX_FILENAME_SZ);
}

/* Useful little helper - return the string ptr for a given file */
char *file_name(struct file *file)
{
	return file->f_dentry->d_name.name;
}

/* Some issues with this, coupled closely to fs_lookup.  This assumes that
 * negative dentries are not returned (might differ from linux) */
static struct dentry *do_lookup(struct dentry *parent, char *name)
{
	struct dentry *dentry;
	/* TODO: look up in the dentry cache first */
	dentry = get_dentry(parent->d_sb, parent, name);
	dentry = parent->d_inode->i_op->lookup(parent->d_inode, dentry, 0);
	/* insert in dentry cache */
	/* TODO: if the following are done by us, how do we know the i_ino?
	 * also need to handle inodes that are already read in!  For now, we're
	 * going to have the FS handle it in it's lookup() method: 
	 * - get a new inode
	 * - read in the inode
	 * - put in the inode cache */
	return dentry;
}

/* Walk up one directory, being careful of mountpoints, namespaces, and the top
 * of the FS */
static int climb_up(struct nameidata *nd)
{
	// TODO
	warn("Climbing up (../) in path lookup not supported yet!");
	return 0;
}

/* Update ND such that it represents having followed dentry.  IAW the nd
 * refcnting rules, we need to decref any references that were in there before
 * they get clobbered. */
static int next_link(struct dentry *dentry, struct nameidata *nd)
{
	assert(nd->dentry && nd->mnt);
	/* update the dentry */
	kref_get(&dentry->d_kref, 1);
	kref_put(&nd->dentry->d_kref);
   	nd->dentry = dentry;
	/* update the mount, if we need to */
	if (dentry->d_sb->s_mount != nd->mnt) {
		kref_get(&dentry->d_sb->s_mount->mnt_kref, 1);
		kref_put(&nd->mnt->mnt_kref);
   		nd->mnt = dentry->d_sb->s_mount;
	}
	return 0;
}

static int follow_mount(struct nameidata *nd)
{
	/* Detect mount, follow, etc... (TODO!) */
	return 0;
}

static int follow_symlink(struct nameidata *nd)
{
	/* Detect symlink, LOOKUP_FOLLOW, follow it, etc... (TODO!) */
	return 0;
}

/* Resolves the links in a basic path walk.  0 for success, -EWHATEVER
 * otherwise.  The final lookup is returned via nd. */
static int link_path_walk(char *path, struct nameidata *nd)
{
	struct dentry *link_dentry;
	struct inode *link_inode, *nd_inode;
	char *next_slash;
	char *link = path;
	int error;

	/* skip all leading /'s */
	while (*link == '/')
		link++;
	/* if there's nothing left (null terminated), we're done */
	if (*link == '\0')
		return 0;
	/* TODO: deal with depth and LOOKUP_FOLLOW, important for symlinks */

	/* iterate through each intermediate link of the path.  in general, nd
	 * tracks where we are in the path, as far as dentries go.  once we have the
	 * next dentry, we try to update nd based on that dentry.  link is the part
	 * of the path string that we are looking up */
	while (1) {
		nd_inode = nd->dentry->d_inode;
		if ((error = check_perms(nd_inode, nd->intent)))
			return error;
		/* find the next link, break out if it is the end */
		next_slash = strchr(link, '/');
		if (!next_slash)
			break;
		else
			if (*(next_slash + 1) == '\0') {
				/* trailing slash on the path meant the target is a dir */
				nd->flags |= LOOKUP_DIRECTORY;
				*next_slash = '\0';
				break;
			}
		/* skip over any interim ./ */
		if (!strncmp("./", link, 2)) {
			link = next_slash + 1;
			continue;
		}
		/* Check for "../", walk up */
		if (!strncmp("../", link, 3)) {
			climb_up(nd);
			link = next_slash + 2;
			continue;
		}
		*next_slash = '\0';
		link_dentry = do_lookup(nd->dentry, link);
		*next_slash = '/';
		if (!link_dentry)
			return -ENOENT;
		/* make link_dentry the current step/answer */
		next_link(link_dentry, nd);
		kref_put(&link_dentry->d_kref);	/* do_lookup gave us a refcnt dentry */
		/* we could be on a mountpoint or a symlink - need to follow them */
		follow_mount(nd);
		follow_symlink(nd);
		if (!(nd->dentry->d_inode->i_type & FS_I_DIR))
			return -ENOTDIR;
		/* move through the path string to the next entry */
		link = next_slash + 1;
	}
	/* now, we're on the last link of the path */
	/* if we just want the parent, leave now.  and save the name of the link
	 * (last) and the type (last_type).  Note that using the qstr in this manner
	 * only allows us to use the qstr as long as the path is a valid string. */
	if (nd->flags & LOOKUP_PARENT) {
		/* consider using a slimmer qstr_builder for this */
		nd->last.name = link;
		nd->last.len = strlen(link);
		nd->last.hash = nd->dentry->d_op->d_hash(nd->dentry, &nd->last);
		return 0;
	}
	/* deal with some weird cases with . and .. (completely untested) */
	if (!strcmp(".", link))
		return 0;
	if (!strcmp("..", link))
		return climb_up(nd);
	link_dentry = do_lookup(nd->dentry, link);
	if (!link_dentry)
		return -ENOENT;
	next_link(link_dentry, nd);
	kref_put(&link_dentry->d_kref);	/* do_lookup gave us a refcnt dentry */
	follow_mount(nd);
	follow_symlink(nd);
	/* If we wanted a directory, but didn't get one, error out */
	if ((nd->flags & LOOKUP_DIRECTORY) &&
	   !(nd->dentry->d_inode->i_type & FS_I_DIR))
		return -ENOTDIR;
	return 0;
}

/* Given path, return the inode for the final dentry.  The ND should be
 * initialized for the first call - specifically, we need the intent. 
 * LOOKUP_PARENT and friends go in this flags var.
 *
 * TODO: this should consider the intent.  Note that creating requires writing
 * to the last directory.
 *
 * Need to be careful too.  While the path has been copied-in to the kernel,
 * it's still user input.  */
int path_lookup(char *path, int flags, struct nameidata *nd)
{
	printd("Path lookup for %s\n", path);
	/* we allow absolute lookups with no process context */
	if (path[0] == '/') {			/* absolute lookup */
		if (!current)
			nd->dentry = default_ns.root->mnt_root;
		else
			nd->dentry = current->fs_env.root;	
	} else {						/* relative lookup */
		assert(current);
		/* Don't need to lock on the fs_env since we're reading one item */
		nd->dentry = current->fs_env.pwd;	
	}
	nd->mnt = nd->dentry->d_sb->s_mount;
	/* Whenever references get put in the nd, incref them.  Whenever they are
	 * removed, decref them. */
	kref_get(&nd->mnt->mnt_kref, 1);
	kref_get(&nd->dentry->d_kref, 1);
	nd->flags = flags;
	nd->depth = 0;					/* used in symlink following */
	return link_path_walk(path, nd);	
}

/* Call this after any use of path_lookup when you are done with its results,
 * regardless of whether it succeeded or not.  It will free any references */
void path_release(struct nameidata *nd)
{
	kref_put(&nd->dentry->d_kref);
	kref_put(&nd->mnt->mnt_kref);
}

/* Superblock functions */

/* Helper to alloc and initialize a generic superblock.  This handles all the
 * VFS related things, like lists.  Each FS will need to handle its own things
 * in it's *_get_sb(), usually involving reading off the disc. */
struct super_block *get_sb(void)
{
	struct super_block *sb = kmalloc(sizeof(struct super_block), 0);
	sb->s_dirty = FALSE;
	spinlock_init(&sb->s_lock);
	kref_init(&sb->s_kref, fake_release, 1); /* for the ref passed out */
	TAILQ_INIT(&sb->s_inodes);
	TAILQ_INIT(&sb->s_dirty_i);
	TAILQ_INIT(&sb->s_io_wb);
	SLIST_INIT(&sb->s_anon_d);
	TAILQ_INIT(&sb->s_files);
	sb->s_fs_info = 0; // can override somewhere else
	return sb;
}

/* Final stages of initializing a super block, including creating and linking
 * the root dentry, root inode, vmnt, and sb.  The d_op and root_ino are
 * FS-specific, but otherwise it's FS-independent, tricky, and not worth having
 * around multiple times.
 *
 * Not the world's best interface, so it's subject to change, esp since we're
 * passing (now 3) FS-specific things. */
void init_sb(struct super_block *sb, struct vfsmount *vmnt,
             struct dentry_operations *d_op, unsigned long root_ino,
             void *d_fs_info)
{
	/* Build and init the first dentry / inode.  The dentry ref is stored later
	 * by vfsmount's mnt_root.  The parent is dealt with later. */
	struct dentry *d_root = get_dentry(sb, 0,  "/");	/* probably right */

	/* a lot of here on down is normally done in lookup() or create, since
	 * get_dentry isn't a fully usable dentry.  The two FS-specific settings are
	 * normally inherited from a parent within the same FS in get_dentry, but we
	 * have none here. */
	d_root->d_op = d_op;
	d_root->d_fs_info = d_fs_info;
	struct inode *inode = get_inode(d_root);
	if (!inode)
		panic("This FS sucks!");
	d_root->d_inode = inode;				/* storing the inode's kref here */
	TAILQ_INSERT_TAIL(&inode->i_dentry, d_root, d_alias);	/* weak ref */
	inode->i_ino = root_ino;
	/* TODO: add the inode to the appropriate list (off i_list) */
	/* TODO: do we need to read in the inode?  can we do this on demand? */
	/* if this FS is already mounted, we'll need to do something different. */
	sb->s_op->read_inode(inode);
	/* Link the dentry and SB to the VFS mount */
	vmnt->mnt_root = d_root;				/* ref comes from get_dentry */
	vmnt->mnt_sb = sb;
	/* If there is no mount point, there is no parent.  This is true only for
	 * the rootfs. */
	if (vmnt->mnt_mountpoint) {
		kref_get(&vmnt->mnt_mountpoint->d_kref, 1);	/* held by d_root */
		d_root->d_parent = vmnt->mnt_mountpoint;	/* dentry of the root */
	}
	/* insert the dentry into the dentry cache.  when's the earliest we can?
	 * when's the earliest we should?  what about concurrent accesses to the
	 * same dentry?  should be locking the dentry... */
	dcache_put(d_root); // TODO: should set a d_flag too
}

/* Dentry Functions */

/* Helper to alloc and initialize a generic dentry.  The following needs to be
 * set still: d_op (if no parent), d_fs_info (opt), d_inode, connect the inode
 * to the dentry (and up the d_kref again), maybe dcache_put().  The inode
 * stitching is done in get_inode() or lookup (depending on the FS).
 *
 * If the name is longer than the inline name, it will kmalloc a buffer, so
 * don't worry about the storage for *name after calling this. */
struct dentry *get_dentry(struct super_block *sb, struct dentry *parent,
                          char *name)
{
	assert(name);
	size_t name_len = strnlen(name, MAX_FILENAME_SZ);	/* not including \0! */
	struct dentry *dentry = kmem_cache_alloc(dentry_kcache, 0);
	char *l_name = 0;

	//memset(dentry, 0, sizeof(struct dentry));
	kref_init(&dentry->d_kref, dentry_release, 1);	/* this ref is returned */
	spinlock_init(&dentry->d_lock);
	TAILQ_INIT(&dentry->d_subdirs);
	dentry->d_time = 0;
	kref_get(&sb->s_kref, 1);
	dentry->d_sb = sb;					/* storing a ref here... */
	dentry->d_mount_point = FALSE;
	dentry->d_mounted_fs = 0;
	if (parent)	{						/* no parent for rootfs mount */
		kref_get(&parent->d_kref, 1);
		dentry->d_op = parent->d_op;	/* d_op set in init_sb for parentless */
	}
	dentry->d_parent = parent;
	dentry->d_flags = 0;				/* related to its dcache state */
	dentry->d_fs_info = 0;
	SLIST_INIT(&dentry->d_bucket);
	if (name_len < DNAME_INLINE_LEN) {
		strncpy(dentry->d_iname, name, name_len);
		dentry->d_iname[name_len] = '\0';
		qstr_builder(dentry, 0);
	} else {
		l_name = kmalloc(name_len + 1, 0);
		assert(l_name);
		strncpy(l_name, name, name_len);
		l_name[name_len] = '\0';
		qstr_builder(dentry, l_name);
	}
	/* Catch bugs by aggressively zeroing this (o/w we use old stuff) */
	dentry->d_inode = 0;
	return dentry;
}

/* Adds a dentry to the dcache. */
void dcache_put(struct dentry *dentry)
{
#if 0 /* pending a more thorough review of the dcache */
	/* TODO: should set a d_flag too */
	spin_lock(&dcache_lock);
	SLIST_INSERT_HEAD(&dcache, dentry, d_hash);
	spin_unlock(&dcache_lock);
#endif
}

/* Cleans up the dentry (after ref == 0).  We still may want it, and this is
 * where we should add it to the dentry cache.  (TODO).  For now, we do nothing,
 * since we don't have a dcache.
 * 
 * This has to handle two types of dentries: full ones (ones that had been used)
 * and ones that had been just for lookups - hence the check for d_inode.
 *
 * Note that dentries pin and kref their inodes.  When all the dentries are
 * gone, we want the inode to be released via kref.  The inode has internal /
 * weak references to the dentry, which are not refcounted. */
void dentry_release(struct kref *kref)
{
	struct dentry *dentry = container_of(kref, struct dentry, d_kref);
	printd("Freeing dentry %08p: %s\n", dentry, dentry->d_name.name);
	assert(dentry->d_op);	/* catch bugs.  a while back, some lacked d_op */
	dentry->d_op->d_release(dentry);
	/* TODO: check/test the boundaries on this. */
	if (dentry->d_name.len > DNAME_INLINE_LEN)
		kfree((void*)dentry->d_name.name);
	kref_put(&dentry->d_sb->s_kref);
	if (dentry->d_mounted_fs)
		kref_put(&dentry->d_mounted_fs->mnt_kref);
	if (dentry->d_inode) {
		TAILQ_REMOVE(&dentry->d_inode->i_dentry, dentry, d_alias);
		kref_put(&dentry->d_inode->i_kref);	/* but dentries kref inodes */
	}
	kmem_cache_free(dentry_kcache, dentry);
}

/* Inode Functions */

/* Creates and initializes a new inode.  Generic fields are filled in.
 * FS-specific fields are filled in by the callout.  Specific fields are filled
 * in in read_inode() based on what's on the disk for a given i_no, or when the
 * inode is created (for new objects).
 *
 * i_no is set by the caller.  Note that this means this inode can be for an
 * inode that is already on disk, or it can be used when creating. */
struct inode *get_inode(struct dentry *dentry)
{
	struct super_block *sb = dentry->d_sb;
	/* FS allocs and sets the following: i_op, i_fop, i_pm.pm_op, and any FS
	 * specific stuff. */
	struct inode *inode = sb->s_op->alloc_inode(sb);
	if (!inode) {
		set_errno(current_tf, ENOMEM);
		return 0;
	}
	TAILQ_INSERT_HEAD(&sb->s_inodes, inode, i_sb_list);		/* weak inode ref */
	TAILQ_INIT(&inode->i_dentry);
	TAILQ_INSERT_TAIL(&inode->i_dentry, dentry, d_alias);	/* weak dentry ref*/
	/* one for the dentry->d_inode, one passed out */
	kref_init(&inode->i_kref, inode_release, 2);
	dentry->d_inode = inode;
	inode->i_ino = 0;					/* set by caller later */
	inode->i_blksize = sb->s_blocksize;
	spinlock_init(&inode->i_lock);
	inode->i_sb = sb;
	inode->i_state = 0;					/* need real states, like I_NEW */
	inode->dirtied_when = 0;
	atomic_set(&inode->i_writecount, 0);
	/* Set up the page_map structures.  Default is to use the embedded one.
	 * Might push some of this back into specific FSs.  For now, the FS tells us
	 * what pm_op they want via i_pm.pm_op, which we use when we point i_mapping
	 * to i_pm. */
	inode->i_mapping = &inode->i_pm;
	inode->i_mapping->pm_host = inode;
	radix_tree_init(&inode->i_mapping->pm_tree);
	spinlock_init(&inode->i_mapping->pm_tree_lock);
	inode->i_mapping->pm_flags = 0;
	return inode;
}

/* Helper op, used when creating regular files and directories.  Note we make a
 * distinction between the mode and the file type (for now).  After calling
 * this, call the FS specific version (create or mkdir), which will set the
 * i_ino, the filetype, and do any other FS-specific stuff.  Also note that a
 * lot of inode stuff was initialized in get_inode/alloc_inode.  The stuff here
 * is pertinent to the specific creator (user), mode, and time.  Also note we
 * don't pass this an nd, like Linux does... */
static struct inode *create_inode(struct dentry *dentry, int mode)
{
	/* note it is the i_ino that uniquely identifies a file in the system.
	 * there's a diff between creating an inode (even for an in-use ino) and
	 * then filling it in, and vs creating a brand new one */
	struct inode *inode = get_inode(dentry);
	if (!inode)
		return 0;
	inode->i_mode = mode;
	inode->i_nlink = 1;
	inode->i_size = 0;
	inode->i_blocks = 0;
	inode->i_atime.tv_sec = 0;		/* TODO: now! */
	inode->i_ctime.tv_sec = 0;
	inode->i_mtime.tv_sec = 0;
	inode->i_atime.tv_nsec = 0;		/* are these supposed to be the extra ns? */
	inode->i_ctime.tv_nsec = 0;
	inode->i_mtime.tv_nsec = 0;
	inode->i_bdev = inode->i_sb->s_bdev;
	return inode;
}

/* Create a new disk inode in dir associated with dentry, with the given mode.
 * called when creating a regular file.  dir is the directory/parent.  dentry is
 * the dentry of the inode we are creating.  Note the lack of the nd... */
int create_file(struct inode *dir, struct dentry *dentry, int flags, int mode)
{
	struct inode *new_file = create_inode(dentry, mode);
	if (!new_file)
		return -1;
	dir->i_op->create(dir, dentry, mode, 0);
	/* when we have notions of users, do something here: */
	new_file->i_uid = 0;
	new_file->i_gid = 0;
	/* Not supposed to keep these creation flags */
	new_file->i_flags = flags & ~(O_CREAT|O_TRUNC|O_EXCL|O_NOCTTY);
	kref_put(&new_file->i_kref);
	return 0;
}

/* Creates a new inode for a directory associated with dentry in dir with the
 * given mode. */
int create_dir(struct inode *dir, struct dentry *dentry, int mode)
{
	struct inode *new_dir = create_inode(dentry, mode);
	if (!new_dir)
		return -1;
	dir->i_op->mkdir(dir, dentry, mode);
	/* Make sure my parent tracks me.  This is okay, since no directory (dir)
	 * can have more than one dentry */
	struct dentry *parent = TAILQ_FIRST(&dir->i_dentry);
	assert(parent && parent == TAILQ_LAST(&dir->i_dentry, dentry_tailq));
	/* parent dentry tracks dentry as a subdir, weak reference */
	TAILQ_INSERT_TAIL(&parent->d_subdirs, dentry, d_subdirs_link);
	kref_put(&new_dir->i_kref);
	return 0;
}

/* Returns 0 if the given mode is acceptable for the inode, and an appropriate
 * error code if not.  Needs to be writen, based on some sensible rules, and
 * will also probably use 'current' */
int check_perms(struct inode *inode, int access_mode)
{
	return 0;	/* anything goes! */
}

/* Called after all external refs are gone to clean up the inode.  Once this is
 * called, all dentries pointing here are already done (one of them triggered
 * this via kref_put(). */
void inode_release(struct kref *kref)
{
	struct inode *inode = container_of(kref, struct inode, i_kref);
	inode->i_sb->s_op->destroy_inode(inode);
	kref_put(&inode->i_sb->s_kref);
	assert(inode->i_mapping == &inode->i_pm);
	kmem_cache_free(inode_kcache, inode);
	/* TODO: (BDEV) */
	// kref_put(inode->i_bdev->kref); /* assuming it's a bdev */
}

/* File functions */

/* Read count bytes from the file into buf, starting at *offset, which is increased
 * accordingly, returning the number of bytes transfered.  Most filesystems will
 * use this function for their f_op->read.  Note, this uses the page cache.
 * Want to try out page remapping later on... */
ssize_t generic_file_read(struct file *file, char *buf, size_t count,
                          off_t *offset)
{
	struct page *page;
	int error;
	off_t page_off;
	unsigned long first_idx, last_idx;
	size_t copy_amt;
	char *buf_end;

	/* Consider pushing some error checking higher in the VFS */
	if (!count)
		return 0;
	if (*offset == file->f_dentry->d_inode->i_size)
		return 0; /* EOF */
	/* Make sure we don't go past the end of the file */
	if (*offset + count > file->f_dentry->d_inode->i_size) {
		count = file->f_dentry->d_inode->i_size - *offset;
	}
	page_off = *offset & (PGSIZE - 1);
	first_idx = *offset >> PGSHIFT;
	last_idx = (*offset + count) >> PGSHIFT;
	buf_end = buf + count;
	/* For each file page, make sure it's in the page cache, then copy it out.
	 * TODO: will probably need to consider concurrently truncated files here.*/
	for (int i = first_idx; i <= last_idx; i++) {
		error = file_load_page(file, i, &page);
		assert(!error);	/* TODO: handle ENOMEM and friends */
		copy_amt = MIN(PGSIZE - page_off, buf_end - buf);
		/* TODO: (UMEM) think about this.  if it's a user buffer, we're relying
		 * on current to detect whose it is (which should work for async calls).
		 * Also, need to propagate errors properly...  Probably should do a
		 * user_mem_check, then free, and also to make a distinction between
		 * when the kernel wants a read/write (TODO: KFOP) */
		if (current) {
			memcpy_to_user(current, buf, page2kva(page) + page_off, copy_amt);
		} else {
			memcpy(buf, page2kva(page) + page_off, copy_amt);
		}
		buf += copy_amt;
		page_off = 0;
		page_decref(page);	/* it's still in the cache, we just don't need it */
	}
	assert(buf == buf_end);
	*offset += count;
	return count;
}

/* Write count bytes from buf to the file, starting at *offset, which is increased
 * accordingly, returning the number of bytes transfered.  Most filesystems will
 * use this function for their f_op->write.  Note, this uses the page cache.
 * Changes don't get flushed to disc til there is an fsync, page cache eviction,
 * or other means of trying to writeback the pages. */
ssize_t generic_file_write(struct file *file, const char *buf, size_t count,
                           off_t *offset)
{
	struct page *page;
	int error;
	off_t page_off;
	unsigned long first_idx, last_idx;
	size_t copy_amt;
	const char *buf_end;

	/* Consider pushing some error checking higher in the VFS */
	if (!count)
		return 0;
	/* Extend the file.  Should put more checks in here, and maybe do this per
	 * page in the for loop below. */
	if (*offset + count > file->f_dentry->d_inode->i_size)
		file->f_dentry->d_inode->i_size = *offset + count;
	page_off = *offset & (PGSIZE - 1);
	first_idx = *offset >> PGSHIFT;
	last_idx = (*offset + count) >> PGSHIFT;
	buf_end = buf + count;
	/* For each file page, make sure it's in the page cache, then write it.*/
	for (int i = first_idx; i <= last_idx; i++) {
		error = file_load_page(file, i, &page);
		assert(!error);	/* TODO: handle ENOMEM and friends */
		copy_amt = MIN(PGSIZE - page_off, buf_end - buf);
		/* TODO: (UMEM) (KFOP) think about this.  if it's a user buffer, we're
		 * relying on current to detect whose it is (which should work for async
		 * calls). */
		if (current) {
			memcpy_from_user(current, page2kva(page) + page_off, buf, copy_amt);
		} else {
			memcpy(page2kva(page) + page_off, buf, copy_amt);
		}
		buf += copy_amt;
		page_off = 0;
		page_decref(page);	/* it's still in the cache, we just don't need it */
	}
	assert(buf == buf_end);
	*offset += count;
	return count;
}

/* Opens the file, using permissions from current for lack of a better option.
 * It will attempt to create the file if it does not exist and O_CREAT is
 * specified.  This will return 0 on failure, and set errno.
 * TODO: There's a lot of stuff that we don't do, esp related to permission
 * checking and file truncating.  Create should set errno and propagate it up.*/
struct file *do_file_open(char *path, int flags, int mode)
{
	struct file *file = 0;
	struct dentry *file_d;
	struct inode *parent_i;
	struct nameidata nd_r = {0}, *nd = &nd_r;
	int lookup_flags = LOOKUP_PARENT;
	int error = 0;

	/* lookup the parent */
	nd->intent = flags & (O_RDONLY|O_WRONLY|O_RDWR);
	if (flags & O_CREAT)
		lookup_flags |= LOOKUP_CREATE;
	error = path_lookup(path, lookup_flags, nd);
	if (error) {
		set_errno(current_tf, -error);
		return 0;
	}
	/* see if the target is there, handle accordingly */
	file_d = do_lookup(nd->dentry, nd->last.name); 
	if (!file_d) {
		if (!(flags & O_CREAT)) {
			path_release(nd);
			set_errno(current_tf, ENOENT);
			return 0;
		}
		/* Create the inode/file.  get a fresh dentry too: */
		file_d = get_dentry(nd->dentry->d_sb, nd->dentry, nd->last.name);
		parent_i = nd->dentry->d_inode;
		/* TODO: mode should be & ~umask.  Note that mode only applies to future
		 * opens. */
		if (create_file(parent_i, file_d, flags, mode)) {
			kref_put(&file_d->d_kref);
			path_release(nd);
			return 0;
		}
		dcache_put(file_d);
	} else {	/* the file exists */
		if ((flags & O_CREAT) && (flags & O_EXCL)) {
			/* wanted to create, not open, bail out */
			kref_put(&file_d->d_kref);
			path_release(nd);
			set_errno(current_tf, EACCES);
			return 0;
		}
	}
	/* now open the file (freshly created or if it already existed).  At this
	 * point, file_d is a refcnt'd dentry, regardless of which branch we took.*/
	if (flags & O_TRUNC)
		warn("File truncation not supported yet.");
	file = dentry_open(file_d);		/* sets errno */
	if (!file) {
		kref_put(&file_d->d_kref);
		path_release(nd);
		return 0;
	}
	/* TODO: check the inode's mode (S_XXX) against the flags O_RDWR */
	/* f_mode stores how the FILE is open, regardless of the mode */
	file->f_mode = flags & (O_RDONLY|O_WRONLY|O_RDWR);
	kref_put(&file_d->d_kref);
	path_release(nd);
	return file;
}

/* Checks to see if path can be accessed via mode.  Doesn't do much now.  This
 * is an example of decent error propagation from the lower levels via int
 * retvals. */
int do_file_access(char *path, int mode)
{
	struct nameidata nd_r = {0}, *nd = &nd_r;
	int retval = 0;
	/* TODO: when we care about access, do stuff here.  Need to be a bit careful
	 * about how intent works with access (F_OK, R_OK, etc) and open (O_RDONLY)
	 */
	nd->intent = mode;
	retval = path_lookup(path, 0, nd);
	path_release(nd);	
	return retval;
}

/* Opens and returns the file specified by dentry */
struct file *dentry_open(struct dentry *dentry)
{
	struct inode *inode;
	struct file *file = kmem_cache_alloc(file_kcache, 0);
	if (!file) {
		set_errno(current_tf, ENOMEM);
		return 0;
	}
	inode = dentry->d_inode;
	/* one for the ref passed out, and *none* for the sb TAILQ */
	kref_init(&file->f_kref, file_release, 1);
	/* Add to the list of all files of this SB */
	TAILQ_INSERT_TAIL(&inode->i_sb->s_files, file, f_list);
	kref_get(&dentry->d_kref, 1);
	file->f_dentry = dentry;
	kref_get(&inode->i_sb->s_mount->mnt_kref, 1);
	file->f_vfsmnt = inode->i_sb->s_mount;		/* saving a ref to the vmnt...*/
	file->f_op = inode->i_fop;
	file->f_flags = inode->i_flags;				/* just taking the inode vals */
	file->f_mode = inode->i_mode;
	file->f_pos = 0;
	file->f_uid = inode->i_uid;
	file->f_gid = inode->i_gid;
	file->f_error = 0;
//	struct event_poll_tailq		f_ep_links;
	spinlock_init(&file->f_ep_lock);
	file->f_fs_info = 0;						/* prob overriden by the fs */
	file->f_mapping = inode->i_mapping;
	file->f_op->open(inode, file);
	return file;
}

/* Closes a file, fsync, whatever else is necessary.  Called when the kref hits
 * 0.  Note that the file is not refcounted on the s_files list, nor is the
 * f_mapping refcounted (it is pinned by the i_mapping). */
void file_release(struct kref *kref)
{
	struct file *file = container_of(kref, struct file, f_kref);

	struct super_block *sb = file->f_dentry->d_sb;
	spin_lock(&sb->s_lock);
	TAILQ_REMOVE(&sb->s_files, file, f_list);
	spin_unlock(&sb->s_lock);

	/* TODO: fsync (BLK).  also, we may want to parallelize the blocking that
	 * could happen in here (spawn kernel threads)... */
	file->f_op->release(file->f_dentry->d_inode, file);
	/* Clean up the other refs we hold */
	kref_put(&file->f_dentry->d_kref);
	kref_put(&file->f_vfsmnt->mnt_kref);
	kmem_cache_free(file_kcache, file);
}

/* Page cache functions */

/* Looks up the index'th page in the page map, returning an incref'd reference,
 * or 0 if it was not in the map. */
struct page *pm_find_page(struct page_map *pm, unsigned long index)
{
	spin_lock(&pm->pm_tree_lock);
	struct page *page = (struct page*)radix_lookup(&pm->pm_tree, index);
	if (page)
		page_incref(page);
	spin_unlock(&pm->pm_tree_lock);
	return page;
}

/* Attempts to insert the page into the page_map, returns 0 for success, or an
 * error code if there was one already (EEXIST) or we ran out of memory
 * (ENOMEM).  On success, this will preemptively lock the page, and will also
 * store a reference to the page in the pm. */
int pm_insert_page(struct page_map *pm, unsigned long index, struct page *page)
{
	int error = 0;
	spin_lock(&pm->pm_tree_lock);
	error = radix_insert(&pm->pm_tree, index, page);
	if (!error) {
		page_incref(page);
		page->pg_flags |= PG_LOCKED;
		page->pg_mapping = pm;
		page->pg_index = index;
		pm->pm_num_pages++;
	}
	spin_unlock(&pm->pm_tree_lock);
	return error;
}

/* Removes the page, including its reference.  Not sure yet what interface we
 * want to this (pm and index or page), and this has never been used.  There are
 * also issues with when you want to call this, since a page in the cache may be
 * mmap'd by someone else. */
int pm_remove_page(struct page_map *pm, struct page *page)
{
	void *retval;
	warn("pm_remove_page() hasn't been thought through or tested.");
	spin_lock(&pm->pm_tree_lock);
	retval = radix_delete(&pm->pm_tree, page->pg_index);
	spin_unlock(&pm->pm_tree_lock);
	assert(retval == (void*)page);
	page_decref(page);
	page->pg_mapping = 0;
	page->pg_index = 0;
	pm->pm_num_pages--;
	return 0;
}

/* Makes sure the index'th page from file is loaded in the page cache and
 * returns its location via **pp.  Note this will give you a refcnt'd reference.
 * This may block! TODO: (BLK) */
int file_load_page(struct file *file, unsigned long index, struct page **pp)
{
	struct page_map *pm = file->f_mapping;
	struct page *page;
	int error;
	bool page_was_mapped = TRUE;

	page = pm_find_page(pm, index);
	while (!page) {
		/* kpage_alloc, since we want the page to persist after the proc
		 * dies (can be used by others, until the inode shuts down). */
		if (kpage_alloc(&page))
			return -ENOMEM;
		/* might want to initialize other things, perhaps in page_alloc() */
		page->pg_flags = 0;
		error = pm_insert_page(pm, index, page);
		switch (error) {
			case 0:
				page_was_mapped = FALSE;
				break;
			case -EEXIST:
				/* the page was mapped already (benign race), just get rid of
				 * our page and try again (the only case that uses the while) */
				page_decref(page);
				page = pm_find_page(pm, index);
				break;
			default:
				/* something is wrong, bail out! */
				page_decref(page);
				return error;
		}
	}
	*pp = page;
	/* if the page was in the map, we need to do some checks, and might have to
	 * read in the page later.  If the page was freshly inserted to the pm by
	 * us, we skip this since we are the one doing the readpage(). */
	if (page_was_mapped) {
		/* is it already here and up to date?  if so, we're done */
		if (page->pg_flags & PG_UPTODATE)
			return 0;
		/* if not, try to lock the page (could BLOCK) */
		lock_page(page);
		/* we got it, is our page still in the cache?  check the mapping.  if
		 * not, start over, perhaps with EAGAIN and outside support */
		if (!page->pg_mapping)
			panic("Page is not in the mapping!  Haven't implemented this!");
		/* double check, are we up to date?  if so, we're done */
		if (page->pg_flags & PG_UPTODATE) {
			unlock_page(page);
			return 0;
		}
	}
	/* if we're here, the page is locked by us, and it needs to be read in */
	assert(page->pg_mapping == pm);
	error = pm->pm_op->readpage(file, page);
	assert(!error);
	/* Try to sleep on the IO.  The page will be unlocked when the IO is done */
	lock_page(page);
	unlock_page(page);
	assert(page->pg_flags & PG_UPTODATE);
	return 0;
}

/* Process-related File management functions */

/* Given any FD, get the appropriate file, 0 o/w */
struct file *get_file_from_fd(struct files_struct *open_files, int file_desc)
{
	struct file *retval = 0;
	if (file_desc < 0)
		return 0;
	spin_lock(&open_files->lock);
	if (file_desc < open_files->max_fdset) {
		if (GET_BITMASK_BIT(open_files->open_fds->fds_bits, file_desc)) {
			/* while max_files and max_fdset might not line up, we should never
			 * have a valid fdset higher than files */
			assert(file_desc < open_files->max_files);
			retval = open_files->fd[file_desc];
			assert(retval);
			kref_get(&retval->f_kref, 1);
		}
	}
	spin_unlock(&open_files->lock);
	return retval;
}

/* Remove FD from the open files, if it was there, and return f.  Currently,
 * this decref's f, so the return value is not consumable or even usable.  This
 * hasn't been thought through yet. */
struct file *put_file_from_fd(struct files_struct *open_files, int file_desc)
{
	struct file *file = 0;
	if (file_desc < 0)
		return 0;
	spin_lock(&open_files->lock);
	if (file_desc < open_files->max_fdset) {
		if (GET_BITMASK_BIT(open_files->open_fds->fds_bits, file_desc)) {
			/* while max_files and max_fdset might not line up, we should never
			 * have a valid fdset higher than files */
			assert(file_desc < open_files->max_files);
			file = open_files->fd[file_desc];
			open_files->fd[file_desc] = 0;
			CLR_BITMASK_BIT(open_files->open_fds->fds_bits, file_desc);
			/* the if case is due to files (stdin) without a *file yet */
			if (file)
				kref_put(&file->f_kref);
		}
	}
	spin_unlock(&open_files->lock);
	return file;
}

/* Inserts the file in the files_struct, returning the corresponding new file
 * descriptor, or an error code.  We currently grab the first open FD. */
int insert_file(struct files_struct *open_files, struct file *file)
{
	int slot = -1;
	spin_lock(&open_files->lock);
	for (int i = 0; i < open_files->max_fdset; i++) {
		if (GET_BITMASK_BIT(open_files->open_fds->fds_bits, i))
			continue;
		slot = i;
		SET_BITMASK_BIT(open_files->open_fds->fds_bits, slot);
		assert(slot < open_files->max_files && open_files->fd[slot] == 0);
		kref_get(&file->f_kref, 1);
		open_files->fd[slot] = file;
		if (slot >= open_files->next_fd)
			open_files->next_fd = slot + 1;
		break;
	}
	if (slot == -1)	/* should expand the FD array and fd_set */
		warn("Ran out of file descriptors, deal with me!");
	spin_unlock(&open_files->lock);
	return slot;
}

/* Closes all open files.  Mostly just a "put" for all files.  If cloexec, it
 * will only close files that are opened with O_CLOEXEC. */
void close_all_files(struct files_struct *open_files, bool cloexec)
{
	struct file *file;
	spin_lock(&open_files->lock);
	for (int i = 0; i < open_files->max_fdset; i++) {
		if (GET_BITMASK_BIT(open_files->open_fds->fds_bits, i)) {
			/* while max_files and max_fdset might not line up, we should never
			 * have a valid fdset higher than files */
			assert(i < open_files->max_files);
			file = open_files->fd[i];
			if (cloexec && !(file->f_flags | O_CLOEXEC))
				continue;
			open_files->fd[i] = 0;
			/* the if case is due to files (stdin) without a *file yet */
			if (file)
				kref_put(&file->f_kref);
			CLR_BITMASK_BIT(open_files->open_fds->fds_bits, i);
		}
	}
	spin_unlock(&open_files->lock);
}

/* Inserts all of the files from src into dst, used by sys_fork(). */
void clone_files(struct files_struct *src, struct files_struct *dst)
{
	struct file *file;
	spin_lock(&src->lock);
	spin_lock(&dst->lock);
	for (int i = 0; i < src->max_fdset; i++) {
		if (GET_BITMASK_BIT(src->open_fds->fds_bits, i)) {
			/* while max_files and max_fdset might not line up, we should never
			 * have a valid fdset higher than files */
			assert(i < src->max_files);
			file = src->fd[i];
			SET_BITMASK_BIT(dst->open_fds->fds_bits, i);
			assert(i < dst->max_files && dst->fd[i] == 0);
			dst->fd[i] = file;
			/* the if case is due to files (stdin) without a *file yet */
			if (file)
				kref_get(&file->f_kref, 1);
		}
	}
	spin_unlock(&dst->lock);
	spin_unlock(&src->lock);
}
