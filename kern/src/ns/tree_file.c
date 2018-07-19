/* Copyright (c) 2018 Google Inc
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * tree_file: structs and helpers for a tree-based filesystem for 9ns devices.
 */

#include <tree_file.h>
#include <kmalloc.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <error.h>

/* Adds to the LRU if it was not on it.
 *
 * Caller holds the TF lock or o/w knows it has the only ref to tf. */
static void __add_to_lru(struct tree_file *tf)
{
	struct walk_cache *wc = &tf->tfs->wc;

	if (tf->flags & TF_F_ON_LRU)
		return;
	tf->flags |= TF_F_ON_LRU;
	spin_lock(&wc->lru_lock);
	list_add_tail(&tf->lru, &wc->lru);
	spin_unlock(&wc->lru_lock);
}

/* Removes from the LRU if it was on it.
 *
 * Caller holds the TF lock or o/w knows it has the only ref to tf. */
static void __remove_from_lru(struct tree_file *tf)
{
	struct walk_cache *wc = &tf->tfs->wc;

	if (!(tf->flags & TF_F_ON_LRU))
		return;
	assert(kref_refcnt(&tf->kref) == 0);
	tf->flags &= ~TF_F_ON_LRU;
	spin_lock(&wc->lru_lock);
	list_del(&tf->lru);
	spin_unlock(&wc->lru_lock);
}

/* Caller holds the parent's qlock.  Separate helpers here in case we track
 * nr_children. */
static void __add_to_parent_list(struct tree_file *parent,
                                 struct tree_file *child)
{
	list_add(&child->siblings, &parent->children);
}

/* Caller holds the parent's qlock */
static void __remove_from_parent_list(struct tree_file *parent,
                                      struct tree_file *child)
{
	list_del(&child->siblings);
}

/* Safely grabs a kref on TF, possibly resurrecting from 0, at which point the
 * file would be on an LRU list.  This syncs with tree removers, including
 * unlinking from the tree on remove and LRU cache pruners.  Returns true if we
 * got the ref, false if we lost and the file was disconnected. */
bool tf_kref_get(struct tree_file *tf)
{
	spin_lock(&tf->lifetime);
	if (tf->flags & TF_F_DISCONNECTED) {
		spin_unlock(&tf->lifetime);
		return false;
	}
	__remove_from_lru(tf);
	__kref_get(&tf->kref, 1);
	tf->flags |= TF_F_HAS_BEEN_USED;
	spin_unlock(&tf->lifetime);
	return true;
}

void tf_kref_put(struct tree_file *tf)
{
	kref_put(&tf->kref);
}

static void __tf_free(struct tree_file *tf)
{
	struct tree_file *parent = tf->parent;
	struct tree_filesystem *tfs = tf->tfs;

	tf->tfs->tf_ops.free(tf);
	if (tf->flags & TF_F_IS_ROOT) {
		assert(tfs->root == tf);
		assert(!parent);
	}
	cleanup_fs_file((struct fs_file*)tf);
	kfree(tf);
	/* the reason for decreffing the parent now is for convenience on releasing.
	 * When we unlink the child from the LRU pruner, we don't want to release
	 * the parent immediately while we hold the parent's qlock (and other
	 * locks). */
	if (parent)
		tf_kref_put(parent);
}

static void __tf_free_rcu(struct rcu_head *head)
{
	struct tree_file *tf = container_of(head, struct tree_file, rcu);

	__tf_free(tf);
}

static void tf_release(struct kref *kref)
{
	struct tree_file *tf = container_of(kref, struct tree_file, kref);

	assert(!(tf->flags & TF_F_NEGATIVE));

	spin_lock(&tf->lifetime);
	if (kref_refcnt(&tf->kref) > 0) {
		/* Someone resurrected after we decreffed to 0. */
		assert(!(tf->flags & TF_F_ON_LRU));
		spin_unlock(&tf->lifetime);
		return;
	}
	if (!(tf->flags & (TF_F_DISCONNECTED | TF_F_IS_ROOT))) {
		/* It's possible that we paused before locking, then another thread
		 * upped, downed, and put it on the LRU list already.  The helper deals
		 * with that. */
		__add_to_lru(tf);
		spin_unlock(&tf->lifetime);
		return;
	}
	spin_unlock(&tf->lifetime);
	/* Need RCU, since we could have had a reader who saw the object and still
	 * needs to try to kref (and fail).  call_rcu, since we can't block. */
	call_rcu(&tf->rcu, __tf_free_rcu);
}

static unsigned long hash_string(const char *name)
{
	unsigned long hash = 5381;

	for (const char *p = name; *p; p++) {
		/* hash * 33 + c, djb2's technique */
		hash = ((hash << 5) + hash) + *p;
	}
	return hash;
}

static void wc_init(struct walk_cache *wc)
{
	spinlock_init(&wc->lru_lock);
	INIT_LIST_HEAD(&wc->lru);
	spinlock_init(&wc->ht_lock);
	wc->ht = wc->static_ht;
	hash_init_hh(&wc->hh);
	for (int i = 0; i < wc->hh.nr_hash_lists; i++)
		INIT_HLIST_HEAD(&wc->ht[i]);
}

static void wc_destroy(struct walk_cache *wc)
{
	assert(list_empty(&wc->lru));
	for (int i = 0; i < wc->hh.nr_hash_lists; i++)
		assert(hlist_empty(&wc->ht[i]));
	if (wc->ht != wc->static_ht)
		kfree(wc->ht);
}

/* Looks up the child of parent named 'name' in the walk cache hash table.
 * Caller needs to hold an rcu read lock. */
static struct tree_file *wc_lookup_child(struct tree_file *parent,
                                         const char *name)
{
	struct walk_cache *wc = &parent->tfs->wc;
	unsigned long hash_val = hash_string(name);
	struct hlist_head *bucket;
	struct tree_file *i;

	bucket = &wc->ht[hash_val % wc->hh.nr_hash_bits];
	hlist_for_each_entry_rcu(i, bucket, hash) {
		/* Note 'i' is an rcu protected pointer.  That deref is safe.  i->parent
		 * is also a pointer that in general we want to protect.  In this case,
		 * even though we don't dereference it, we want a pointer that is good
		 * enough to dereference so we can do the comparison. */
		if (rcu_dereference(i->parent) != parent)
			continue;
		/* The file's name should never change while it is in the table, so no
		 * need for a seq-reader.  Can't assert though, since there are valid
		 * reasons for other seq lockers. */
		if (!strcmp(tree_file_to_name(i), name))
			return i;
	}
	return NULL;
}

/* Caller should hold the parent's qlock */
static void wc_insert_child(struct tree_file *parent, struct tree_file *child)
{
	struct walk_cache *wc = &parent->tfs->wc;
	unsigned long hash_val = hash_string(tree_file_to_name(child));
	struct hlist_head *bucket;

	assert(child->parent == parent);	/* catch bugs from our callers */
	/* TODO: consider bucket locks and/or growing the HT.  Prob need a seq_ctr
	 * in the WC, used on the read side during resizing.  Removal probably would
	 * need something other than the bucket lock too (confusion about which
	 * bucket during the op). */
	spin_lock(&wc->ht_lock);
	bucket = &wc->ht[hash_val % wc->hh.nr_hash_bits];
	hlist_add_head_rcu(&child->hash, bucket);
	spin_unlock(&wc->ht_lock);
}

/* Caller should hold the parent's qlock */
static void wc_remove_child(struct tree_file *parent, struct tree_file *child)
{
	struct walk_cache *wc = &parent->tfs->wc;

	assert(child->parent == parent);	/* catch bugs from our callers */
	spin_lock(&wc->ht_lock);
	hlist_del_rcu(&child->hash);
	spin_unlock(&wc->ht_lock);
}

/* Helper: returns a refcounted pointer to the potential parent.  May return 0.
 *
 * Caller needs to qlock the parent and recheck tf->parent.  Callers always need
 * to get a kref on the parent.  We can rcu-read the parent and *attempt* to
 * qlock under rcu, but we might block.  Then, say, the TF and the parent got
 * removed, and *then* we get the qlock.  We're too late. */
static struct tree_file *__tf_get_potential_parent(struct tree_file *tf)
{
	struct tree_file *parent;

	rcu_read_lock();
	parent = rcu_dereference(tf->parent);
	if (!parent) {
		/* the root of the tree has no parent */
		rcu_read_unlock();
		return NULL;
	}
	if (!tf_kref_get(parent))
		parent = NULL;
	rcu_read_unlock();
	return parent;
}

/* Returns a refcounted and qlocked parent for child.  NULL on failure. */
static struct tree_file *get_locked_and_kreffed_parent(struct tree_file *child)
{
	struct tree_file *parent;

	parent = __tf_get_potential_parent(child);
	if (!parent)
		return NULL;
	qlock(&parent->file.qlock);
	/* Checking the parent == child->parent isn't enough here.  That works for
	 * rename, but not removal/unlink.  Older versions of TF code cleared
	 * child->parent, but now that's dealt with in tf_free.
	 *
	 * We're doing a lockless peek at child's flags.  We hold the potential
	 * parent's lock, so if they are ours, no one will be messing with the
	 * disconnected flag.  If they are messing with it, then parent !=
	 * child->parent.  Also, once disconnected is set, it is never clear. */
	if ((child->flags & TF_F_DISCONNECTED) || (parent != child->parent)) {
		qunlock(&parent->file.qlock);
		tf_kref_put(parent);
		return NULL;
	}
	return parent;
}

static bool __mark_disconnected(struct tree_file *tf)
{
	bool need_to_free;

	spin_lock(&tf->lifetime);
	tf->flags |= TF_F_DISCONNECTED;
	__remove_from_lru(tf);
	need_to_free = kref_refcnt(&tf->kref) == 0;
	spin_unlock(&tf->lifetime);
	return need_to_free;
}

/* Disconnects child from the in-memory tree structure (i.e. only the front
 * end).  Caller holds the parent qlock.  Assumes child is a child of parent.
 * Once you disconnect, you can't touch the child object.
 *
 * Racing with concurrent lookups, who might grab a ref if they get in before
 * DISCONNECTED, and racing with release (after ref = 0), who might free, if it
 * was already unlinked. */
static void __disconnect_child(struct tree_file *parent,
                               struct tree_file *child)
{
	bool need_to_free;

	need_to_free = __mark_disconnected(child);
	/* Note child->parent is still set.  We clear that in __tf_free. */
	__remove_from_parent_list(parent, child);
	wc_remove_child(parent, child);
	if (need_to_free)
		call_rcu(&child->rcu, __tf_free_rcu);
}

/* Backend will need to fill in dir, except for name.  Note this has a kref ==
 * 0, but is not on the LRU yet. */
struct tree_file *tree_file_alloc(struct tree_filesystem *tfs,
                                  struct tree_file *parent, const char *name)
{
	struct tree_file *tf;

	tf = kzmalloc(sizeof(struct tree_file), MEM_WAIT);
	fs_file_init((struct fs_file*)tf, name, &tfs->fs_ops);
	kref_init(&tf->kref, tf_release, 0);
	spinlock_init(&tf->lifetime);
	/* Need to set the parent early on, even if the child isn't linked yet, so
	 * that the TFS ops know who the parent is. */
	tf->parent = parent;
	if (parent)
		kref_get(&parent->kref, 1);
	INIT_LIST_HEAD(&tf->children);
	tf->can_have_children = true;
	tf->tfs = tfs;
	return tf;
}

/* Callers must hold the parent's qlock. */
static void __link_child(struct tree_file *parent, struct tree_file *child)
{
	/* Devices may have already increffed ("+1 for existing").  Those that don't
	 * need to be on the LRU.  We haven't linked to the parent yet, so we hold
	 * the only ref.  Once we unlock in __add_to_lru, we're discoverable via
	 * that list, even though we're not linked.  The lru pruner is careful to
	 * not muck with the parent's or wc linkage without qlocking the parent,
	 * which we currently hold. */
	if (kref_refcnt(&child->kref) == 0)
		__add_to_lru(child);
	/* This was set in tree_file_alloc */
	assert(child->parent == parent);
	__add_to_parent_list(parent, child);
	wc_insert_child(parent, child);
}

static void neuter_directory(struct tree_file *dir)
{
	bool throw = false;

	qlock(&dir->file.qlock);
	if (dir->tfs->tf_ops.has_children(dir))
		throw = true;
	dir->can_have_children = false;
	qunlock(&dir->file.qlock);
	if (throw)
		error(ENOTEMPTY, "can't remove dir with children");
}

/* Unlink a child from the tree.  Last ref will clean it up, which will not be
 * us.  Caller should hold krefs on the child and parent.  The child's kref will
 * actually keep the parent's alive, but all practical callers will have the
 * parent kreff'ed and qlocked - which is required to ensure the child is still
 * a child of parent. */
static void __unlink_child(struct tree_file *parent, struct tree_file *child)
{
	/* Need to make sure concurrent creates/renames do not add children to
	 * directories that are unlinked.  Note we don't undo the neutering if the
	 * backend fails. */
	if (tree_file_is_dir(child))
		neuter_directory(child);
	/* The ramfs backend will probably decref the "+1 for existing" ref.
	 * This is OK.  If that was the last ref, the child will briefly be on
	 * the LRU list (which ramfs ignores).  When we disconnect, we'll yank
	 * the child back off the list and then free it (after rcu). */
	parent->tfs->tf_ops.unlink(parent, child);
	__disconnect_child(parent, child);
}

/* Talks to the backend and ensures a tree_file for the child exists, either
 * positive or negative.  Throws an error; doesn't return NULL.
 *
 * This returns with an rcu read lock sufficient to protect the returned TF, but
 * it is *not* kreffed.  It could be a negative entry, which is not kreffed.
 *
 * It is possible that at the moment we qunlock, someone comes along and removes
 * the entry (LRU prune or file removal (for positive entries)).  That's fine -
 * we have an rcu read lock, just like during a regular walk, when the removal
 * could happen anyways. */
static struct tree_file *lookup_child_entry(struct tree_file *parent,
                                            const char *name)
{
	ERRSTACK(1);
	struct tree_file *child;

	qlock(&parent->file.qlock);
	child = wc_lookup_child(parent, name);
	if (child) {
		/* Since we last looked, but before we qlocked, someone else added our
		 * entry. */
		rcu_read_lock();
		qunlock(&parent->file.qlock);
		return child;
	}
	child = tree_file_alloc(parent->tfs, parent, name);
	if (waserror()) {
		/* child wasn't fully created, so freeing it may be tricky, esp on the
		 * device ops side (might see something they never created). */
		__tf_free(child);
		qunlock(&parent->file.qlock);
		nexterror();
	}
	parent->tfs->tf_ops.lookup(parent, child);
	poperror();
	__link_child(parent, child);
	rcu_read_lock();
	qunlock(&parent->file.qlock);
	return child;
}

/* Walks a tree filesystem from 'from' for array of null-terminated names.
 * Devices will often use tree_chan_walk(), but can use this directly if they
 * want more control.
 *
 * Returns the WQ of qids.  On complete/successful walks, we hang a refcnted TF
 * on wq->clone.
 *
 * Walks can return intermediate results, which is when there is an error but we
 * have walked at least once.  The partial return is useful for namec(), which
 * can succeed if something was mounted on an intermediate QID.  Walks that have
 * no results set error and return NULL, which is what namec() expects. */
struct walkqid *tree_file_walk(struct tree_file *from, char **name,
                               unsigned int nname)
{
	ERRSTACK(2);
	struct tree_file *at, *next;
	struct tree_filesystem *tfs = from->tfs;
	struct walkqid *wq;

	wq = kzmalloc(sizeof(struct walkqid) + nname * sizeof(struct qid),
				  MEM_WAIT);
	/* A walk with zero names means "make me a copy."  If we go through the
	 * regular walker, our usual tf_kref_get will fail - similar to failing if
	 * we got a walk for "foo/../" during a concurrent removal of ourselves.
	 * We'll let a walk of zero names work, but if you provide any names, the
	 * actual walk must happen.
	 *
	 * This is tricky, and confused me a little.  We're returning a *TF* through
	 * wq->clone, not a chan, and that is refcounted.  Normally for chans that
	 * end up with wq->clone == c, we do not incref the object hanging off the
	 * chan (see k/d/d/eventfd.c), since there is just one chan with a kreffed
	 * object hanging off e.g. c->aux.  But here, wq->clone is considered a
	 * distinct refcnt to some TF, and it might be 'from.'  Our *caller* needs
	 * to deal with the "wq->clone == from_chan", since they deal with chans.
	 * We deal with tree files. */
	if (!nname) {
		kref_get(&from->kref, 1);
		wq->clone = (struct chan*)from;
		return wq;
	}
	if (waserror()) {
		kfree(wq);
		poperror();
		return NULL;
	}
	rcu_read_lock();
	at = from;
	for (int i = 0; i < nname; i++) {
		/* Walks end if we reach a regular file, i.e. you can't walk through a
		 * file, only a dir.  But even if there are more names, the overall walk
		 * might succeed.  E.g. a directory could be mounted on top of the
		 * current file we're at.  That's just not our job. */
		if (tree_file_is_file(at)) {
			if (i == 0)
				error(ENOTDIR, "initial walk from a file");
			break;
		}
		/* Normally, symlinks stop walks, and namec's walk() will deal with it.
		 * We allow walks 'through' symlinks, but only for .. and only for the
		 * first name.  This is for relative lookups so we can find the parent
		 * of a symlink. */
		if (tree_file_is_symlink(at)) {
			if (i != 0)
				break;
			if (strcmp(name[i], ".."))
				error(ELOOP, "walk from a symlink that wasn't ..");
		}
		if (!caller_has_tf_perms(at, O_READ)) {
			if (i == 0)
				error(EACCES, "missing perm for lookup");
			break;
		}
		if (!strcmp(name[i], ".")) {
			wq->qid[wq->nqid++] = tree_file_to_qid(at);
			continue;
		}
		if (!strcmp(name[i], "..")) {
			next = rcu_dereference(at->parent);
			if (!next) {
				if (tree_file_is_root(at)) {
					wq->qid[wq->nqid++] = tree_file_to_qid(at);
					/* I think namec should never give us DOTDOT that isn't at
					 * the end of the names array.  Though devwalk() seems to
					 * expect it. */
					if (i != nname - 1)
						warn("Possible namec DOTDOT bug, call for help!");
					continue;
				}
				/* We lost our parent due to a removal/rename.  We might have
				 * walked enough for our walk to succeed (e.g.  there's a mount
				 * point in the WQ), so we can return what we have.  Though if
				 * we've done nothing, it's a failure.
				 *
				 * Note the removal could have happened a long time ago:
				 * consider an O_PATH open, then namec_from().
				 *
				 * We also could walk up and see the *new* parent during
				 * rename().  For instance, /src/x/../y could get the old /src/y
				 * or the new /dst/y.  If we want to avoid that, then we'll need
				 * some sort of sync with rename to make sure we don't get the
				 * new one.  Though this can happen with namec_from(), so I'm
				 * not sure I care. */
				if (i == 0)
					error(ENOENT, "file lost its parent during lookup");
				break;
			}
			at = next;
			wq->qid[wq->nqid++] = tree_file_to_qid(at);
			continue;
		}
		next = wc_lookup_child(at, name[i]);
		if (!next) {
			/* TFSs with no backend have the entire tree in the WC HT. */
			if (!tfs->tf_ops.lookup) {
				if (i == 0)
					error(ENOENT, "file does not exist");
				break;
			}
			/* Need to hold a kref on 'at' before we rcu_read_unlock().  Our
			 * TFS op might block. */
			if (!tf_kref_get(at)) {
				if (i == 0)
					error(ENOENT, "file was removed during lookup");
				/* WQ has a qid for 'at' from a previous loop, but since we
				 * can't walk to it, we should unwind it. */
				wq->nqid--;
				break;
			}
			rcu_read_unlock();
			/* propagate the error only on the first name.  Note we run the
			 * 'else' case, and run the poperror case for non-errors and
			 * non-name=0-errors. */
			if (waserror()) {
				if (i == 0) {
					tf_kref_put(at);
					nexterror();
				}
			} else {
				/* This returns with an rcu_read_lock protecting 'next' */
				next = lookup_child_entry(at, name[i]);
			}
			tf_kref_put(at);
			poperror();
			assert(next);
		}
		if (tree_file_is_negative(next)) {
			/* lockless peek.  other flag users aren't atomic, etc. */
			if (!(next->flags & TF_F_HAS_BEEN_USED)) {
				spin_lock(&next->lifetime);
				next->flags |= TF_F_HAS_BEEN_USED;
				spin_unlock(&next->lifetime);
			}
			if (i == 0)
				error(ENOENT, "file does not exist");
			break;
		}
		at = next;
		wq->qid[wq->nqid++] = tree_file_to_qid(at);
	}
	if (wq->nqid == nname || tree_file_is_symlink(at)) {
		if (!tf_kref_get(at)) {
			/* We need to undo our last result as if we never saw it. */
			wq->nqid--;
			if (wq->nqid == 0)
				error(ENOENT, "file was removed during lookup");
		} else {
			/* Hanging the refcounted TF off the wq->clone, which is cheating.
			 * Our walker shim knows to expect this. */
			wq->clone = (struct chan*)at;
		}
	}
	rcu_read_unlock();
	poperror();
	return wq;
}

/* Most tree devices will use this for their walk op, similar to devwalk. */
struct walkqid *tree_chan_walk(struct chan *c, struct chan *nc, char **name,
                               unsigned int nname)
{
	struct tree_file *from, *to;
	struct walkqid *wq;

	from = chan_to_tree_file(c);
	wq = tree_file_walk(from, name, nname);
	if (!wq)
		return NULL;
	if (!wq->clone)
		return wq;
	if (!nc)
		nc = devclone(c);
	/* Not sure if callers that specify nc must have a chan from our device */
	assert(nc->type == c->type);
	to = (struct tree_file*)wq->clone;
	nc->qid = tree_file_to_qid(to);
	chan_set_tree_file(nc, to);
	wq->clone = nc;
	/* We might be returning the same chan, so there's actually just one ref */
	if (wq->clone == c)
		tf_kref_put(chan_to_tree_file(c));
	return wq;
}

/* Creates a tree file under parent with name, omode, and perm.  Returns a
 * kreffed tree_file for the new child.  Throws on error. */
struct tree_file *tree_file_create(struct tree_file *parent, const char *name,
                                   uint32_t perm, char *ext)
{
	ERRSTACK(2);
	struct tree_file *child;
	bool got_ref;
	struct timespec now;

	qlock(&parent->file.qlock);
	if (waserror()) {
		qunlock(&parent->file.qlock);
		nexterror();
	}
	if (!caller_has_tf_perms(parent, O_WRITE))
		error(EACCES, "missing create permission on dir");
	child = wc_lookup_child(parent, name);
	if (child) {
		/* The create(5) message fails if the file exists, which differs from
		 * the syscall.  namec() handles this. */
		if (!tree_file_is_negative(child))
			error(EEXIST, "file exists");
		/* future lookups that find no entry will qlock.  concurrent ones that
		 * see the child, even if disconnected, will see it was negative and
		 * fail. */
		__disconnect_child(parent, child);
	}
	child = tree_file_alloc(parent->tfs, parent, name);
	if (waserror()) {
		__tf_free(child);
		nexterror();
	}
	/* Backend will need to know the ext for its create.  This gets cleaned up
	 * on error. */
	if (perm & DMSYMLINK)
		kstrdup(&child->file.dir.ext, ext);
	/* Backend will need to fill in dir, except for name. */
	parent->tfs->tf_ops.create(parent, child, perm);
	now = nsec2timespec(epoch_nsec());
	__set_acmtime_to(&child->file,
	                 FSF_ATIME | FSF_BTIME | FSF_CTIME | FSF_MTIME, &now);
	poperror();
	/* At this point, the child is visible, so it must be ready to go */
	__link_child(parent, child);
	got_ref = tf_kref_get(child);
	assert(got_ref);	/* we hold the qlock, no one should have removed */
	__set_acmtime_to(&parent->file, FSF_CTIME | FSF_MTIME, &now);
	qunlock(&parent->file.qlock);
	poperror();
	return child;
}

/* Most tree devices will use this for their create op. */
void tree_chan_create(struct chan *c, char *name, int omode, uint32_t perm,
                      char *ext)
{
	struct tree_file *parent, *child;

	parent = chan_to_tree_file(c);
	child = tree_file_create(parent, name, perm, ext);
	c->qid = tree_file_to_qid(child);
	c->mode = openmode(omode);
	chan_set_tree_file(c, child);
	tf_kref_put(parent);
}

struct chan *tree_chan_open(struct chan *c, int omode)
{
	struct tree_file *tf = chan_to_tree_file(c);

	if ((c->qid.type & QTDIR) && (omode & O_WRITE))
		error(EISDIR, "can't open a dir for writing");
	if (c->qid.type & QTSYMLINK)
		error(ELOOP, "can't open a symlink");
	tree_file_perm_check(tf, omode);
	/* TODO: if we want to support DMEXCL on dir.mode, we'll need to lock/sync
	 * on the fs_file (have a flag for FSF_IS_OPEN, handle in close).  We'll
	 * also need a way to pass it in to the dir.mode during create/wstat/etc. */
	if (omode & O_TRUNC)
		fs_file_truncate(&tf->file, 0);
	c->mode = openmode(omode);
	c->flag |= COPEN;
	c->offset = 0;
	return c;
}

void tree_chan_close(struct chan *c)
{
	struct tree_file *tf = chan_to_tree_file(c);

	tf_kref_put(tf);
}

void tree_file_remove(struct tree_file *child)
{
	ERRSTACK(1);
	struct tree_file *parent;

	parent = get_locked_and_kreffed_parent(child);
	if (!parent)
		error(ENOENT, "%s had no parent", tree_file_to_name(child));
	if (waserror()) {
		qunlock(&parent->file.qlock);
		tf_kref_put(parent);
		nexterror();
	}
	if (!caller_has_tf_perms(parent, O_WRITE))
		error(EACCES, "missing remove perm for dir");
	__unlink_child(parent, child);
	__set_acmtime(&parent->file, FSF_CTIME | FSF_MTIME);
	poperror();
	qunlock(&parent->file.qlock);
	tf_kref_put(parent);
}

void tree_chan_remove(struct chan *c)
{
	ERRSTACK(1);
	struct tree_file *tf = chan_to_tree_file(c);

	/* sysremove expects a chan that is disconnected from the device, regardless
	 * of whether or not we fail.  See sysremove(); it will clear type, ensuring
	 * our close is never called. */
	if (waserror()) {
		chan_set_tree_file(c, NULL);
		tf_kref_put(tf);	/* The ref from the original walk */
		nexterror();
	}
	tree_file_remove(tf);
	chan_set_tree_file(c, NULL);
	tf_kref_put(tf);	/* The ref from the original walk */
	poperror();
}

static bool is_descendant_of(struct tree_file *descendant,
                             struct tree_file *ancestor)
{
	if (!tree_file_is_dir(ancestor))
		return false;
	rcu_read_lock();
	for (struct tree_file *i = rcu_dereference(descendant->parent);
	     i;
	     i = rcu_dereference(i->parent)) {
		if (i == ancestor) {
			rcu_read_unlock();
			return true;
		}
	}
	rcu_read_unlock();
	return false;
}

/* Caller should hold the rename mutex.  This qlocks a and b, which can be the
 * same file, and this handles lock ordering.  Recall the rule: parents before
 * children, and otherwise only the renamer can lock. */
static void qlock_tree_files(struct tree_file *a, struct tree_file *b)
{
	if (a == b) {
		qlock(&a->file.qlock);
		return;
	}
	if (is_descendant_of(a, b)) {
		qlock(&b->file.qlock);
		qlock(&a->file.qlock);
	} else {
		qlock(&a->file.qlock);
		qlock(&b->file.qlock);
	}
}

static void qunlock_tree_files(struct tree_file *a, struct tree_file *b)
{
	if (a != b)
		qunlock(&a->file.qlock);
	qunlock(&b->file.qlock);
}

/* Higher layers (namec) ensure that tf and new_parent are in the same device.
 * The device (e.g. #mnt) ensures that they are in the same instance.
 * new_parent could be the parent of tf; everything should still work if the
 * move is within the same directory. */
void tree_file_rename(struct tree_file *tf, struct tree_file *new_parent,
                      const char *name, int flags)
{
	ERRSTACK(1);
	struct tree_file *old_parent;
	struct tree_file *prev_dst;
	struct timespec now;

	old_parent = __tf_get_potential_parent(tf);
	if (!old_parent)
		error(ENOENT, "renamed file had no parent");
	/* global mtx helps with a variety of weird races, including the "can't move
	 * to a subdirectory of yourself" case and the lock ordering of parents
	 * (locks flow from parent->child). */
	qlock(&tf->tfs->rename_mtx);
	qlock_tree_files(old_parent, new_parent);
	if (waserror()) {
		qunlock_tree_files(old_parent, new_parent);
		qunlock(&tf->tfs->rename_mtx);
		tf_kref_put(old_parent);
		nexterror();
	};
	if (old_parent != tf->parent)
		error(ENOENT, "renamed file lost its parent");
	/* Probably a namec bug (that code isn't written yet). */
	assert(tree_file_is_dir(new_parent));
	if (!new_parent->can_have_children)
		error(ENOENT, "target dir being removed");
	if (!caller_has_tf_perms(old_parent, O_WRITE))
		error(EACCES, "missing remove perm for src dir");
	if (!caller_has_tf_perms(new_parent, O_WRITE))
		error(EACCES, "missing create perm for dst dir");
	/* can't move tf to one of its subdirs */
	if (is_descendant_of(new_parent, tf))
		error(EINVAL, "can't rename to a child directory");
	/* We hold new_parent's qlock, so there's no worry about prev_dst
	 * disappearing, so no need for an rcu read lock. */
	prev_dst = wc_lookup_child(new_parent, name);
	if (prev_dst) {
		if (tree_file_is_dir(prev_dst)) {
			if (!tree_file_is_dir(tf))
				error(EISDIR, "can't rename a file onto a dir");
			/* We need to ensure prev_dst is childless and remains so.  That
			 * requires grabbing its qlock, but there's a potential lock
			 * ordering issue with old_parent.  We could have this:
			 * new_parent/dst/x/y/z/old_parent/src.  That will fail, but we need
			 * to check for that case instead of grabbing prev_dst's qlock. */
			if (is_descendant_of(prev_dst, old_parent))
				error(ENOTEMPTY, "old_parent descends from dst");
			neuter_directory(prev_dst);
		} else {
			if (tree_file_is_dir(tf))
				error(ENOTDIR, "can't rename a dir onto a file");
		}
	}
	/* We check with the backend first, so that it has a chance to fail early.
	 * Once we make the changes to the front end, lookups can see the effects of
	 * the change, which we can't roll back.  Since we hold the parents' qlocks,
	 * no one should be able to get the info from the backend either (lookups
	 * that require the backend, readdir, etc). */
	tf->tfs->tf_ops.rename(tf, old_parent, new_parent, name, flags);
	/* Similar to __disconnect_child, we don't clear tf->parent.  rcu readers at
	 * TF will be able to walk up (with ..).  Same with namec_from an FD.  If we
	 * atomically replace tf->parent, we should be good.  See tree_file_walk().
	 *
	 * Further, we don't mark the tf disconnected.  Someone might lookup from
	 * the old location, and that's fine.  We just don't want issues with
	 * decrefs. */
	__remove_from_parent_list(old_parent, tf);
	wc_remove_child(old_parent, tf);
	synchronize_rcu();
	/* Now, no new lookups will find it at the old location.  That change is not
	 * atomic wrt src, but it will be wrt dst.  Importantly, no one will see
	 * /path/to/old_parent/new_basename */
	fs_file_change_basename((struct fs_file*)tf, name);
	/* We're clobbering the old_parent ref, which we'll drop later */
	rcu_assign_pointer(tf->parent, new_parent);
	kref_get(&new_parent->kref, 1);
	__add_to_parent_list(new_parent, tf);
	wc_insert_child(new_parent, tf);
	/* Now both the prev_dst (if it existed) or the tf file are in the walk
	 * cache / HT and either could have been looked up by a concurrent reader.
	 * Readers will always get one or the other, but never see nothing.  This is
	 * the atomic guarantee of rename. */
	if (prev_dst) {
		__remove_from_parent_list(new_parent, prev_dst);
		wc_remove_child(new_parent, prev_dst);
		synchronize_rcu();
		/* Now no one can find prev_dst.  Someone might still have a ref, or it
		 * might be on the LRU list (if kref == 0).  Now we can mark
		 * disconnected.  Had we disconnected earlier, then lookup code would
		 * see that and treat it as a failure.  Using rcu and putting the
		 * complexity in rename was easier and simpler than changing lookup.
		 *
		 * We still need RCU here for freeing the prev_dst.  We could have had
		 * an LRU pruner, etc, looking.  The synchronize_rcu above only dealt
		 * with lookups via parent in this function. */
		if (__mark_disconnected(prev_dst))
			call_rcu(&prev_dst->rcu, __tf_free_rcu);
	}
	now = nsec2timespec(epoch_nsec());
	__set_acmtime_to(&old_parent->file, FSF_CTIME | FSF_MTIME, &now);
	__set_acmtime_to(&new_parent->file, FSF_CTIME | FSF_MTIME, &now);
	/* Can we unlock earlier?  No.  We need to at least hold new_parent's qlock,
	 * which was the parent of old_dst, until old_dst is marked disconnected.
	 * Even though old_dst is removed from new_parent's HT, it is still in the
	 * LRU list. */
	qunlock_tree_files(old_parent, new_parent);
	qunlock(&tf->tfs->rename_mtx);
	poperror();
	tf_kref_put(old_parent);	/* the original tf->parent ref we clobbered */
	tf_kref_put(old_parent);	/* the one we grabbed when we started */
}

void tree_chan_rename(struct chan *c, struct chan *new_p_c, const char *name,
                      int flags)
{
	struct tree_file *tf = chan_to_tree_file(c);
	struct tree_file *new_parent = chan_to_tree_file(new_p_c);

	tree_file_rename(tf, new_parent, name, flags);
}

/* dri is a pointer to the chan->dri, which is a count of how many directory
 * entries that have been read from this chan so far.  9ns handles it; we just
 * need to increment it for every successful entry.  Note we ignore offset. */
ssize_t tree_file_readdir(struct tree_file *parent, void *ubuf, size_t n,
                          off64_t offset, int *dri)
{
	ERRSTACK(1);
	struct tree_file *i;
	size_t dir_amt, so_far = 0;
	uint8_t *write_pos = ubuf;
	int child_nr = 0;

	qlock(&parent->file.qlock);
	if (waserror()) {
		qunlock(&parent->file.qlock);
		nexterror();
	}
	list_for_each_entry(i, &parent->children, siblings) {
		if (child_nr++ < *dri)
			continue;
		qlock(&i->file.qlock);
		dir_amt = convD2M(&i->file.dir, write_pos, n - so_far);
		qunlock(&i->file.qlock);
		if (dir_amt <= BIT16SZ) {
			if (!so_far)
				error(EINVAL, "buffer to small for readdir");
			break;
		}
		write_pos += dir_amt;
		so_far += dir_amt;
		assert(n - so_far >= 0);
		(*dri)++;
	}
	/* If we care about directory atime, we can do that here. (if so_far) */
	qunlock(&parent->file.qlock);
	poperror();
	return so_far;
}

/* Note this only works for backend-less TFSs.  It calls tree_file_readdir,
 * which only looks at the frontend's tree. */
size_t tree_chan_read(struct chan *c, void *ubuf, size_t n, off64_t offset)
{
	struct tree_file *tf = chan_to_tree_file(c);

	if (tree_file_is_dir(tf))
		return tree_file_readdir(tf, ubuf, n, offset, &c->dri);
	return fs_file_read(&tf->file, ubuf, n, offset);
}

size_t tree_chan_write(struct chan *c, void *ubuf, size_t n, off64_t offset)
{
	struct tree_file *tf = chan_to_tree_file(c);

	/* sysfile.c:rwrite checked the chan's type. */
	assert(!tree_file_is_dir(tf));
	return fs_file_write(&tf->file, ubuf, n, offset);
}

size_t tree_chan_stat(struct chan *c, uint8_t *m_buf, size_t m_buf_sz)
{
	struct tree_file *tf = chan_to_tree_file(c);

	return fs_file_stat(&tf->file, m_buf, m_buf_sz);
}

size_t tree_chan_wstat(struct chan *c, uint8_t *m_buf, size_t m_buf_sz)
{
	struct tree_file *tf = chan_to_tree_file(c);

	return fs_file_wstat(&tf->file, m_buf, m_buf_sz);
}

struct fs_file *tree_chan_mmap(struct chan *c, struct vm_region *vmr, int prot,
                               int flags)
{
	struct fs_file *f = &chan_to_tree_file(c)->file;

	/* TODO: In the future, we'll check the prot, establish hooks with the VMR,
	 * and other things, mostly in something like fs_file_mmap, which will be
	 * able to handle mmaping something that doesn't use the page cache.  For
	 * now, I'm aggressively qlocking to catch bugs. */
	qlock(&f->qlock);
	if ((prot & PROT_WRITE) && (flags & MAP_SHARED))
		f->flags |= FSF_DIRTY;
	qunlock(&f->qlock);
	return f;
}

/* Given a tree file, construct a chan that points to the TF for the given
 * device.  Careful with this - it's for bootstrapping. */
struct chan *tree_file_alloc_chan(struct tree_file *tf, struct dev *dev,
                                  char *name)
{
	struct chan *c;

	c = newchan();
	c->type = dev - devtab;
	c->name = newcname(name);
	kref_get(&tf->kref, 1);
	chan_set_tree_file(c, tf);
	c->qid = tree_file_to_qid(tf);
	return c;
}

/* Caller needs to set its customizable fields: tf_ops, pm_ops, etc.  root is
 * created with a ref of 1, but needs filled in by the particular TFS. */
void tfs_init(struct tree_filesystem *tfs)
{
	wc_init(&tfs->wc);
	qlock_init(&tfs->rename_mtx);
	tfs->root = tree_file_alloc(tfs, NULL, ".");
	tfs->root->flags |= TF_F_IS_ROOT;
	assert(!(tfs->root->flags & TF_F_ON_LRU));
	__kref_get(&tfs->root->kref, 1);
}

void tfs_destroy(struct tree_filesystem *tfs)
{
	tfs->root = NULL;	/* was just freed in __tf_free() */
	wc_destroy(&tfs->wc);
}

/* For every file except root, we hold our parent's qlock (root has no parent).
 * This prevents TF's removal/rename and any changes to tf->parent. */
static void tf_dfs_cb(struct tree_file *tf, void (*cb)(struct tree_file *tf))
{
	struct tree_file *child;

	if (tree_file_is_dir(tf)) {
		qlock(&tf->file.qlock);
		/* Note we don't have a kref on our child's TF - we have a weak
		 * reference (the list membership).  We hold the parent's qlock, which
		 * prevents removal/unlinking/disconnecting/etc.  The child's membership
		 * on the LRU list can change repeatedly.
		 *
		 * If we want to avoid holding the parent's qlock, we could grab a kref
		 * on the child.  However, our list walk would be in jeopardy - both
		 * child and temp could be removed from the list.  So we'd need to qlock
		 * the parent, grab krefs on all children, and put them on a local list.
		 * Also, grabbing krefs on our children will muck with the LRU list;
		 * when we're done, it would be like we sorted the LRU list in the DFS
		 * order. */
		list_for_each_entry(child, &tf->children, siblings)
			tf_dfs_cb(child, cb);
		qunlock(&tf->file.qlock);
	}
	if (!tree_file_is_negative(tf))
		cb(tf);
}

/* Runs CB on all in-memory, non-negative TFs from the TFS in a depth-first
 * search.  Compared to the other CB walkers (purge, LRU, etc), this never
 * removes/prunes/disconnects a TF from the tree.  You can use it for syncing FS
 * trees or pruning page maps (i.e. discarding non-dirty pages).
 *
 * One thing to note is that it qlocks the tree as part of its DFS.  We can
 * change that, but at a slight cost in complexity and tampering with the LRU
 * list.  Translation: we can do it if there's a performance problem.
 *
 * The tfs->root reference is also weak.  It's up to the user to make sure
 * there is no concurrent tfs_destroy.  Typically, if we're called on any
 * mounted device, we're fine (e.g. #tmpfs) or a device that is never destroyed
 * (e.g. #kfs). */
void tfs_frontend_for_each(struct tree_filesystem *tfs,
                           void (*cb)(struct tree_file *tf))
{
	tf_dfs_cb(tfs->root, cb);
}

/* We should be single-user, so no need for concurrency protections.  I keep
 * them around for paranoia/future use/documentation. */
static void tf_dfs_purge(struct tree_file *tf, void (*cb)(struct tree_file *tf))
{
	struct tree_file *child, *temp;

	if (tree_file_is_dir(tf)) {
		qlock(&tf->file.qlock);
		/* Note we don't have a kref on TF, and one of our children should
		 * decref *us* to 0.  We aren't disconnected yet, and we can't be
		 * removed (parent's qlock is held), so we'll just end up on the LRU
		 * list, which is OK.
		 *
		 * Our child should remove itself from our list, so we need the _safe.
		 *
		 * Also note that the child decrefs us in a call_rcu.  CB can block, and
		 * technically so can qlock, so we might run RCU callbacks while
		 * qlocked.  We'll need to rcu_barrier so that our children's decrefs
		 * occur before we remove ourselves from our parent. */
		list_for_each_entry_safe(child, temp, &tf->children, siblings)
			tf_dfs_purge(child, cb);
		qunlock(&tf->file.qlock);
	}
	rcu_barrier();
	/* ramfs will drop the "+1 for existing" ref here */
	if (!tree_file_is_negative(tf))
		cb(tf);
	spin_lock(&tf->lifetime);
	/* should be unused, with no children.  we have a ref on root, to keep the
	 * TFS around while we destroy the tree. */
	assert(kref_refcnt(&tf->kref) == 0 || tree_file_is_root(tf));
	/* This mark prevents new lookups.  We'll disconnect it shortly. */
	tf->flags |= TF_F_DISCONNECTED;
	spin_unlock(&tf->lifetime);
	if (tf->parent)
		__disconnect_child(tf->parent, tf);
}

/* Purges all in-memory TFs from the TFS in a depth-first search, both positive
 * and negative.  We run CB on all non-negative TFs.  It's up to the caller to
 * ensure there is no concurrency.
 *
 * The caller must make sure they have an extra ref on tfs->root to keep the
 * TFS around while it gets destroyed.  The root TF will get freed when it is
 * released, unlike other TFs that are still connected.
 *
 * This is for devices that want to destroy themselves, such as an unmounted
 * #tmpfs or #gtfs/#mnt, that want to do some extra work (e.g. sync).
 * Typically, those devices will call this after they have no users (e.g. mounts
 * or open chans). */
void tfs_frontend_purge(struct tree_filesystem *tfs,
                        void (*cb)(struct tree_file *tf))
{
	assert(kref_refcnt(&tfs->root->kref) > 0);
	tf_dfs_purge(tfs->root, cb);
}

static void print_tf(struct tree_file *tf)
{
	if (tree_file_is_negative(tf)) {
		printk("%-10s: Negative\n", tree_file_to_name(tf));
		return;
	}
	printk("%-10s: Q: %5d, R: %2d, U %s, %c%o %s\n",
		   tree_file_to_name(tf),
		   tf->file.dir.qid.path,
		   kref_refcnt(&tf->kref),
		   tf->file.dir.uid,
		   tree_file_is_dir(tf) ? 'd' :
								tf->file.dir.mode & DMSYMLINK ? 'l' : '-',
		   tf->file.dir.mode & S_PMASK,
		   tf->file.dir.mode & DMSYMLINK ? tf->file.dir.ext : ""
		   );
}

static void dump_tf(struct tree_file *tf, int tabs)
{
	struct tree_file *child;

	if (!!(tf->file.dir.mode & DMSYMLINK) !=
	    !!(tf->file.dir.qid.type & QTSYMLINK))
		warn("%s has differing symlink bits", tree_file_to_name(tf));

	print_lock();
	for (int i = 0; i < tabs; i++)
		printk("    ");
	print_tf(tf);
	if (tree_file_is_dir(tf)) {
		for (int i = 0; i < tabs; i++)
			printk("    ");
		printk("---------------------\n");
		list_for_each_entry(child, &tf->children, siblings)
			dump_tf(child, tabs + 1);
	}
	print_unlock();
}

void __tfs_dump(struct tree_filesystem *tfs)
{
	dump_tf(tfs->root, 0);
}

/* Runs a callback on every non-negative TF on the LRU list, for a given
 * snapshot of the LRU list.  The CB returns true if it wants us to attempt to
 * free the TF.  One invariant is that we can never remove a TF from the tree
 * while it is dirty; it is the job of the CB to maintain that.  Note the CB can
 * run on a TF as soon as that TF was linked to the parent (see lookup).
 *
 * The work list is a list of strong refs.  We need to keep one in case the file
 * is disconnected while we're running our CBs.  Since we incref, we yank from
 * the LRU list.  We could design the rest of the TF code so that we stay on the
 * LRU list throughout, but I like the invariant of "kref == 0 IFF on LRU".
 *
 * Since we're only on one list at a time ('wc->lru' or 'work'), we can use the
 * lru list_head in the TF.  We know that so long as we hold our kref on a TF,
 * no one will attempt to put it back on the LRU list. */
void tfs_lru_for_each(struct tree_filesystem *tfs, bool cb(struct tree_file *),
                      size_t max_tfs)
{
	struct list_head work = LIST_HEAD_INIT(work);
	struct walk_cache *wc = &tfs->wc;
	struct tree_file *tf, *temp, *parent;
	size_t nr_tfs = 0;

	/* We can have multiple LRU workers in flight, though a given TF will be on
	 * only one CB list at a time. */
	spin_lock(&wc->lru_lock);
	list_for_each_entry_safe(tf, temp, &wc->lru, lru) {
		/* lockless peak at the flag.  once it's NEGATIVE, it never goes back */
		if (tree_file_is_negative(tf))
			continue;
		/* Normal lock order is TF -> LRU.  Best effort is fine for LRU. */
		if (!spin_trylock(&tf->lifetime))
			continue;
		/* Can't be disconnected and on LRU */
		assert(!(tf->flags & TF_F_DISCONNECTED));
		assert((tf->flags & TF_F_ON_LRU));
		tf->flags &= ~TF_F_ON_LRU;
		list_del(&tf->lru);
		__kref_get(&tf->kref, 1);
		/* The 'used' bit is the what allows us to detect a user in between our
		 * callback and the disconnection/freeing.  It's a moot point if the CB
		 * returns false. */
		tf->flags &= ~TF_F_HAS_BEEN_USED;
		spin_unlock(&tf->lifetime);
		list_add_tail(&tf->lru, &work);
		if (++nr_tfs >= max_tfs)
			break;
	}
	spin_unlock(&wc->lru_lock);

	/* We have a snapshot of the LRU list.  As soon as we unlocked a file,
	 * someone could incref it (e.g. to something > 1), and they'll set the used
	 * bit.  That won't abort the CB.  New TFs could be added to the LRU list.
	 * Those are ignored for this pass. */
	list_for_each_entry_safe(tf, temp, &work, lru) {
		if (!cb(tf)) {
			list_del(&tf->lru);
			tf_kref_put(tf);
			continue;
		}
	}

	/* Now we have a list of victims to be removed, so long as they haven't been
	 * used since. */
	list_for_each_entry_safe(tf, temp, &work, lru) {
		parent = get_locked_and_kreffed_parent(tf);
		if (!parent) {
			list_del(&tf->lru);
			tf_kref_put(tf);
			continue;
		}
		spin_lock(&tf->lifetime);
		if (tf->flags & TF_F_HAS_BEEN_USED) {
			spin_unlock(&tf->lifetime);
			qunlock(&parent->file.qlock);
			tf_kref_put(parent);
			list_del(&tf->lru);
			tf_kref_put(tf);
			continue;
		}
		tf->flags |= TF_F_DISCONNECTED;
		/* We hold a ref, so it shouldn't have found its way back on LRU */
		assert(!(tf->flags & TF_F_ON_LRU));
		spin_unlock(&tf->lifetime);
		__remove_from_parent_list(parent, tf);
		wc_remove_child(parent, tf);
		/* If we get tired of unlocking and relocking, we could see if the next
		 * parent is the current parent before unlocking. */
		qunlock(&parent->file.qlock);
		tf_kref_put(parent);
	}
	/* Now we have a list of refs that are all disconnected, kref == 1 (because
	 * no one used them since we increffed them when they were LRU, which was
	 * when the refcnt was 0).  Each TF has a ref on its parent btw, so parents
	 * will never be on the LRU list.  (leaves only).
	 *
	 * We need to synchronize_rcu() too, since we could have had lockless
	 * lookups that have pointers to TF and are waiting to notice that it is
	 * disconnected. */
	synchronize_rcu();
	list_for_each_entry_safe(tf, temp, &work, lru) {
		assert(kref_refcnt(&tf->kref) == 1);
		list_del(&tf->lru);
		/* We could decref, but instead we can directly free.  We know the ref
		 * == 1 and it is disconnected.  Directly freeing bypasses call_rcu. */
		__tf_free(tf);
	}
}

/* Does a one-cycle 'clock' algorithm to detect use.  On a given pass, we either
 * clear HAS_BEEN_USED xor we remove it.  For negative entries, that bit is used
 * when we look at an entry (use it), compared to positive entries, which is
 * used when we get a reference.  (we never get refs on negatives). */
void tfs_lru_prune_neg(struct tree_filesystem *tfs)
{
	struct list_head work = LIST_HEAD_INIT(work);
	struct walk_cache *wc = &tfs->wc;
	struct tree_file *tf, *temp, *parent;

	spin_lock(&wc->lru_lock);
	list_for_each_entry_safe(tf, temp, &wc->lru, lru) {
		if (!tree_file_is_negative(tf))
			continue;
		if (!spin_trylock(&tf->lifetime))
			continue;
		if (tf->flags & TF_F_HAS_BEEN_USED) {
			tf->flags &= ~TF_F_HAS_BEEN_USED;
			spin_unlock(&tf->lifetime);
			continue;
		}
		rcu_read_lock();	/* holding a spinlock, but just to be clear. */
		parent = rcu_dereference(tf->parent);
		/* Again, inverting the lock order, so best effort trylock */
		if (!canqlock(&parent->file.qlock)) {
			rcu_read_unlock();
			spin_unlock(&tf->lifetime);
			continue;
		}
		__remove_from_parent_list(parent, tf);
		wc_remove_child(parent, tf);
		qunlock(&parent->file.qlock);
		/* We're off the list, but our kref == 0 still.  We can break that
		 * invariant since we have the only ref and are about to free the TF. */
		tf->flags &= ~TF_F_ON_LRU;
		list_del(&tf->lru);
		spin_unlock(&tf->lifetime);
		list_add_tail(&tf->lru, &work);
	}
	spin_unlock(&wc->lru_lock);
	/* Now we have a list of refs that are all unlinked (but actually not
	 * flagged DISCONNECTED; that's only necessary for positives), kref == 0
	 * (because they are negatives).
	 *
	 * We need to synchronize_rcu() too, since we could have had lockless
	 * lookups that have pointers to TF, and they may even mark HAS_BEEN_USED.
	 * Too late. */
	synchronize_rcu();
	list_for_each_entry_safe(tf, temp, &work, lru) {
		assert(kref_refcnt(&tf->kref) == 0);
		list_del(&tf->lru);
		__tf_free(tf);
	}
}
