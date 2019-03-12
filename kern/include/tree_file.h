/* Copyright (c) 2018 Google Inc
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * tree_file: structs and helpers to for a tree-based filesystem for 9ns
 * devices.
 */

#pragma once

#include <fs_file.h>
#include <ns.h>
#include <list.h>
#include <rcu.h>

struct tree_file;
struct tree_filesystem;

/* The walk cache encapsulates a couple things related to caching, similar to
 * the dentry cache in the VFS.
 * - the parent -> child links in a hash table
 * - the LRU list, consisting of all tree files with kref == 0
 *
 * LRU notes.  Once a TF gets linked into a tree:
 * - a TF is on the LRU IFF its refcnt == 0
 * - a TF is on the LRU only if it is in the tree
 * - any time kref gets set to 0, we consider putting it on the LRU (if not
 *   DISCONNECTED / in the tree).  anytime it is increffed from 0, we yank it.
 * - NEG entries are always kref == 0, and are on the LRU list if they are in
 *   the tree.  They are never increffed, only rcu-read.
 */
struct walk_cache {
	spinlock_t			lru_lock;
	struct list_head		lru;
	spinlock_t			ht_lock;
	struct hash_helper		hh;		/* parts are rcu-read */
	struct hlist_head		*ht;
	struct hlist_head		static_ht[HASH_INIT_SZ];
};

/* All ops that operate on a parent have the parent qlocked.
 *
 * free(): the TF is being freed.  Might be called on TFs that the TFS is
 * unaware of (e.g. lookup threw an error).
 *
 * unlink(): child is being unlinked from the parent.
 *
 * lookup(): backends fill in the child TF with whatever it knows about the
 * entry.  This function is optional.  TFSs whose entire tree is in the frontend
 * (e.g.  ramfs) don't do lookups from the non-existent backend.  This
 * structure, positive or negative, will be used until it is unlinked.
 *
 * create(): given parent and it's initialized child struct, create the file and
 * do whatever else you need (e.g. 9p backend does the create).  Same as lookup,
 * the TFS needs to fill in child->dir, except for name.  Note we don't pass
 * omode.  That's handled in the device's front-end create: open the chan after
 * creating.
 *
 * rename(): rename / move TF from the old_parent to the new_parent, and change
 * the name to name (NULL == no change).  Note that the rename op does *not* set
 * the TF's name yet.  Be careful changing any fields in the TF, since it is
 * still in old_parent's HT.
 *
 * has_children(): TFSs that use the front end as a cache (e.g. #mnt) need to
 * check with their backend to know if there are children or not.  Checking the
 * children list isn't necessarily sufficient.
 */
struct tree_file_ops {
	void (*free)(struct tree_file *tf);
	void (*unlink)(struct tree_file *parent, struct tree_file *child);
	void (*lookup)(struct tree_file *parent, struct tree_file *child);
	void (*create)(struct tree_file *parent, struct tree_file *child,
		       int perm);
	void (*rename)(struct tree_file *tf, struct tree_file *old_parent,
	               struct tree_file *new_parent, const char *name,
		       int flags);
	bool (*has_children)(struct tree_file *parent);
};

struct tree_filesystem {
	struct walk_cache		wc;
	struct tree_file_ops		tf_ops;
	struct fs_file_ops		fs_ops;
	qlock_t				rename_mtx;
	struct tree_file		*root;
	void				*priv;
};

/* The tree_file is an fs_file (i.e. the first struct field) that exists in a
 * tree filesystem with parent/child relationships.
 *
 * Children hold krefs on their parents.  Parents have weak refs (i.e. not a
 * kref) on their children.  Parent's have a list of *known* children.  The
 * backend of the FS (e.g. the other side of #mnt) may know of other children.
 * In this way, the parent's list is a cache.  For devices like a ramfs or any
 * dynamic, completely tree-based system (e.g. #srv), the tree filesystem is the
 * authoritative FS; there is no backend.
 *
 * If we know of a parent-child relationship (i.e. it's in the tree_filesystem),
 * then the following invariants are true:
 * - The child is in the parent's list.
 * - The child->parent == the parent's tree_file
 * - There is an entry in the walk cache from {parent,name} -> child_tree_file.
 * - The backend FS (if there is one) knows about the parent-child link
 *
 * To change these, hold the parent's qlock and you must change them all,
 * with a slight exception: you must *set* child->parent = parent under the
 * lock.  You may *clear* that reference (and decref the parent) outside the
 * parent's qlock when freeing the child.  At this point, there are no
 * references to child, it is off all lists and structures, and RCU readers are
 * done.
 *
 * We'll need to make these changes for create, remove, rename, and lookup (add
 * entries (positive or negative) that we didn't know about).  Note that even
 * changing the basename, but not moving, a child during rename requires the
 * parent's qlock.  (It changes the hashtable linkage).
 *
 * Keep in mind that the info in the frontend (tree_files) may be a subset of
 * the real filesystem, which is the backend (e.g. 9p mount, disk structures,
 * etc).  The frontend might not have everything from the backend, typically
 * because of memory reclaim.
 *
 * Readers use the walk cache hash table and child->parent) with RCU read
 * locks.  The linked list of children is only accessed under the parent's
 * qlock.  If a child needs a ref on its parent, it will need to kref_get during
 * rcu lock.  Children hold refs on parents, but a concurrent removal/rename
 * could break that link and decref the parent.
 *
 * Synchronization rules
 * ------------------
 * Hold the lifetime spin lock when:
 * - Changing or relying on the value of flags (e.g., during kcref upping)
 * - Upping a kref from 0 during a lookup
 * - Changing membership on LRU lists (e.g. during release or kcref upping)
 *   The lock ordering is entry -> LRU list.  Note the LRU pruner will lock the
 *   list first, then *trylock* the entry *and* the parent, skipping on failure.
 *
 * Refcounting rules: (subject to a device's whims)
 * - All walked or attached chans (distinct; those that will be closed in the
 *   device) have a refcounted tree_file.
 * - The tree_files do not have to be in a lookup structure (parent's list /
 *   hash table); they could have been unlinked/removed and waiting for the
 *   final close.
 * - tree_files can have 0 references.  Typically, files that are unopened
 *   (technically, unwalked) will have their kref == 0.  On release, they can
 *   stay in the tree/lookup system, and they are added to an LRU list.
 * - negative tree_files (i.e. names for files that don't exist) always have a
 *   refcnt == 0, and are candidates for being pruned at all times.
 *
 * Lock ordering:
 * - Note the tree uses the qlock in the FS file.  In essence, they are the same
 *   object (a tree_file is an fs_file).  This qlock is for changing the
 *   contents of the file, whether that's the parent directories links to its
 *   children, a file's contents (via write), or the metadata (dir) of either.
 * - Parent qlock -> hash_table bucketlock
 * - Parent qlock -> child lifetime spinlock -> LRU list spinlock
 *   Note the LRU pruner inverts this ordering, and uses trylocks
 * - Parent qlock -> child qlock (unlink and create/rename).
 * - Qlocking multiple files that aren't parent->child requires the rename_mtx
 */
struct tree_file {
	struct fs_file			file;
	spinlock_t			lifetime;
	int				flags;
	struct kref			kref;
	struct rcu_head			rcu;
	struct tree_file		*parent;	/* rcu protected */
	struct hlist_node		hash;		/* rcu protected */
	struct list_head		siblings;
	struct list_head		children;
	struct list_head		lru;
	bool				can_have_children;
	struct tree_filesystem		*tfs;
};

#define TF_F_DISCONNECTED	(1 << 0)
#define TF_F_NEGATIVE		(1 << 1)
#define TF_F_ON_LRU		(1 << 2)
#define TF_F_IS_ROOT		(1 << 3)
#define TF_F_HAS_BEEN_USED	(1 << 4)

/* Devices can put their tree_files / fs_files whereever they want.  For now,
 * all of them will use aux.  We can make ops for this if we need it. */
static inline struct tree_file *chan_to_tree_file(struct chan *c)
{
	return c->aux;
}

static inline void chan_set_tree_file(struct chan *c, struct tree_file *tf)
{
	c->aux = tf;
}

static inline struct qid tree_file_to_qid(struct tree_file *tf)
{
	return tf->file.dir.qid;
}

static inline bool tree_file_is_dir(struct tree_file *tf)
{
	return tree_file_to_qid(tf).type & QTDIR ? true : false;
}

static inline bool tree_file_is_file(struct tree_file *tf)
{
	return qid_is_file(tree_file_to_qid(tf));
}

static inline bool tree_file_is_symlink(struct tree_file *tf)
{
	return tree_file_to_qid(tf).type & QTSYMLINK ? true : false;
}

static inline bool tree_file_is_negative(struct tree_file *tf)
{
	return tf->flags & TF_F_NEGATIVE ? true : false;
}

static inline bool tree_file_is_root(struct tree_file *tf)
{
	return tf == tf->tfs->root;
}

static inline char *tree_file_to_name(struct tree_file *tf)
{
	return tf->file.dir.name;
}

static inline bool caller_has_tf_perms(struct tree_file *tf, int omode)
{
	return caller_has_file_perms(&tf->file, omode);
}

static inline void tree_file_perm_check(struct tree_file *tf, int omode)
{
	fs_file_perm_check(&tf->file, omode);
}

/* tree_file helpers */
bool tf_kref_get(struct tree_file *tf);
void tf_kref_put(struct tree_file *tf);
struct tree_file *tree_file_alloc(struct tree_filesystem *tfs,
                                  struct tree_file *parent, const char *name);
struct walkqid *tree_file_walk(struct tree_file *from, char **name,
                               unsigned int nname);
struct tree_file *tree_file_create(struct tree_file *parent, const char *name,
                                   uint32_t perm, char *ext);
void tree_file_remove(struct tree_file *child);
void tree_file_rename(struct tree_file *tf, struct tree_file *new_parent,
                      const char *name, int flags);
ssize_t tree_file_readdir(struct tree_file *parent, void *ubuf, size_t n,
                          off64_t offset, int *dri);

/* tree_file helpers that operate on chans */
struct walkqid *tree_chan_walk(struct chan *c, struct chan *nc, char **name,
                               unsigned int nname);
void tree_chan_create(struct chan *c, char *name, int omode, uint32_t perm,
                      char *ext);
struct chan *tree_chan_open(struct chan *c, int omode);
void tree_chan_close(struct chan *c);
void tree_chan_remove(struct chan *c);
void tree_chan_rename(struct chan *c, struct chan *new_p_c, const char *name,
                      int flags);
size_t tree_chan_read(struct chan *c, void *ubuf, size_t n, off64_t offset);
size_t tree_chan_write(struct chan *c, void *ubuf, size_t n, off64_t offset);
size_t tree_chan_stat(struct chan *c, uint8_t *m_buf, size_t m_buf_sz);
size_t tree_chan_wstat(struct chan *c, uint8_t *m_buf, size_t m_buf_sz);
struct fs_file *tree_chan_mmap(struct chan *c, struct vm_region *vmr, int prot,
                               int flags);
unsigned long tree_chan_ctl(struct chan *c, int op, unsigned long a1,
                            unsigned long a2, unsigned long a3,
                            unsigned long a4);

struct chan *tree_file_alloc_chan(struct tree_file *tf, struct dev *dev,
                                  char *name);
void tfs_init(struct tree_filesystem *tfs);
void tfs_destroy(struct tree_filesystem *tfs);
void tfs_frontend_for_each(struct tree_filesystem *tfs,
                           void (*cb)(struct tree_file *tf));
void tfs_frontend_purge(struct tree_filesystem *tfs,
                        void (*cb)(struct tree_file *tf));
void __tfs_dump(struct tree_filesystem *tfs);
void __tfs_dump_tf(struct tree_file *tf);

void tfs_lru_for_each(struct tree_filesystem *tfs, bool cb(struct tree_file *),
                      size_t max_tfs);
void tfs_lru_prune_neg(struct tree_filesystem *tfs);
