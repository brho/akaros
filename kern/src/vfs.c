/* Copyright (c) 2009, 2010 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Default implementations and global values for the VFS. */

#include <vfs.h> // keep this first
#include <ros/errno.h>
#include <sys/queue.h>
#include <assert.h>
#include <stdio.h>
#include <atomic.h>
#include <slab.h>
#include <kmalloc.h>
#include <pmap.h>
#include <umem.h>
#include <smp.h>
#include <ns.h>
#include <fdtap.h>

ssize_t kread_file(struct file_or_chan *file, void *buf, size_t sz)
{
	/* TODO: (KFOP) (VFS kernel read/writes need to be from a ktask) */
	uintptr_t old_ret = switch_to_ktask();
	off64_t dummy = 0;
	ssize_t cpy_amt = foc_read(file, buf, sz, dummy);

	switch_back_from_ktask(old_ret);
	return cpy_amt;
}

/* Reads the contents of an entire file into a buffer, returning that buffer.
 * On error, prints something useful and returns 0 */
void *kread_whole_file(struct file_or_chan *file)
{
	size_t size;
	void *contents;
	ssize_t cpy_amt;

	size = foc_get_len(file);
	contents = kmalloc(size, MEM_WAIT);
	cpy_amt = kread_file(file, contents, size);
	if (cpy_amt < 0) {
		printk("Error %d reading file %s\n", get_errno(), foc_to_name(file));
		kfree(contents);
		return 0;
	}
	if (cpy_amt != size) {
		printk("Read %d, needed %d for file %s\n", cpy_amt, size,
		       foc_to_name(file));
		kfree(contents);
		return 0;
	}
	return contents;
}

/* Process-related File management functions */

/* Given any FD, get the appropriate object, 0 o/w. Set incref if you want a
 * reference count (which is a 9ns thing, you can't use the pointer if you
 * didn't incref). */
void *lookup_fd(struct fd_table *fdt, int fd, bool incref)
{
	void *retval = 0;

	if (fd < 0)
		return 0;
	spin_lock(&fdt->lock);
	if (fdt->closed) {
		spin_unlock(&fdt->lock);
		return 0;
	}
	if (fd < fdt->max_fdset) {
		if (GET_BITMASK_BIT(fdt->open_fds->fds_bits, fd)) {
			/* while max_files and max_fdset might not line up, we should never
			 * have a valid fdset higher than files */
			assert(fd < fdt->max_files);
			retval = fdt->fd[fd].fd_chan;
			if (incref)
				chan_incref((struct chan*)retval);
		}
	}
	spin_unlock(&fdt->lock);
	return retval;
}

/* Grow the vfs fd set */
static int grow_fd_set(struct fd_table *open_files)
{
	int n;
	struct file_desc *nfd, *ofd;

	/* Only update open_fds once. If currently pointing to open_fds_init, then
 	 * update it to point to a newly allocated fd_set with space for
 	 * NR_FILE_DESC_MAX */
	if (open_files->open_fds == (struct fd_set*)&open_files->open_fds_init) {
		open_files->open_fds = kzmalloc(sizeof(struct fd_set), 0);
		memmove(open_files->open_fds, &open_files->open_fds_init,
		        sizeof(struct small_fd_set));
	}

	/* Grow the open_files->fd array in increments of NR_OPEN_FILES_DEFAULT */
	n = open_files->max_files + NR_OPEN_FILES_DEFAULT;
	if (n > NR_FILE_DESC_MAX)
		return -EMFILE;
	nfd = kzmalloc(n * sizeof(struct file_desc), 0);
	if (nfd == NULL)
		return -ENOMEM;

	/* Move the old array on top of the new one */
	ofd = open_files->fd;
	memmove(nfd, ofd, open_files->max_files * sizeof(struct file_desc));

	/* Update the array and the maxes for both max_files and max_fdset */
	open_files->fd = nfd;
	open_files->max_files = n;
	open_files->max_fdset = n;

	/* Only free the old one if it wasn't pointing to open_files->fd_array */
	if (ofd != open_files->fd_array)
		kfree(ofd);
	return 0;
}

/* Free the vfs fd set if necessary */
static void free_fd_set(struct fd_table *open_files)
{
	void *free_me;
	if (open_files->open_fds != (struct fd_set*)&open_files->open_fds_init) {
		assert(open_files->fd != open_files->fd_array);
		/* need to reset the pointers to the internal addrs, in case we take a
		 * look while debugging.  0 them out, since they have old data.  our
		 * current versions should all be closed. */
		memset(&open_files->open_fds_init, 0, sizeof(struct small_fd_set));
		memset(&open_files->fd_array, 0, sizeof(open_files->fd_array));

		free_me = open_files->open_fds;
		open_files->open_fds = (struct fd_set*)&open_files->open_fds_init;
		kfree(free_me);

		free_me = open_files->fd;
		open_files->fd = open_files->fd_array;
		kfree(free_me);
	}
}

/* If FD is in the group, remove it, decref it, and return TRUE. */
bool close_fd(struct fd_table *fdt, int fd)
{
	struct chan *chan = 0;
	struct fd_tap *tap = 0;
	bool ret = FALSE;
	if (fd < 0)
		return FALSE;
	spin_lock(&fdt->lock);
	if (fd < fdt->max_fdset) {
		if (GET_BITMASK_BIT(fdt->open_fds->fds_bits, fd)) {
			/* while max_files and max_fdset might not line up, we should never
			 * have a valid fdset higher than files */
			assert(fd < fdt->max_files);
			chan = fdt->fd[fd].fd_chan;
			tap = fdt->fd[fd].fd_tap;
			fdt->fd[fd].fd_chan = 0;
			fdt->fd[fd].fd_tap = 0;
			CLR_BITMASK_BIT(fdt->open_fds->fds_bits, fd);
			if (fd < fdt->hint_min_fd)
				fdt->hint_min_fd = fd;
			ret = TRUE;
		}
	}
	spin_unlock(&fdt->lock);
	/* Need to decref/cclose outside of the lock; they could sleep */
	cclose(chan);
	if (tap)
		kref_put(&tap->kref);
	return ret;
}

static int __get_fd(struct fd_table *open_files, int low_fd, bool must_use_low)
{
	int slot = -1;
	int error;
	bool update_hint = TRUE;
	if ((low_fd < 0) || (low_fd > NR_FILE_DESC_MAX))
		return -EINVAL;
	if (open_files->closed)
		return -EINVAL;	/* won't matter, they are dying */
	if (must_use_low && GET_BITMASK_BIT(open_files->open_fds->fds_bits, low_fd))
		return -ENFILE;
	if (low_fd > open_files->hint_min_fd)
		update_hint = FALSE;
	else
		low_fd = open_files->hint_min_fd;
	/* Loop until we have a valid slot (we grow the fd_array at the bottom of
 	 * the loop if we haven't found a slot in the current array */
	while (slot == -1) {
		for (low_fd; low_fd < open_files->max_fdset; low_fd++) {
			if (GET_BITMASK_BIT(open_files->open_fds->fds_bits, low_fd))
				continue;
			slot = low_fd;
			SET_BITMASK_BIT(open_files->open_fds->fds_bits, slot);
			assert(slot < open_files->max_files &&
			       open_files->fd[slot].fd_chan == 0);
			/* We know slot >= hint, since we started with the hint */
			if (update_hint)
				open_files->hint_min_fd = slot + 1;
			break;
		}
		if (slot == -1)	{
			if ((error = grow_fd_set(open_files)))
				return error;
		}
	}
	return slot;
}

/* Insert a file or chan (obj, chosen by vfs) into the fd group with fd_flags.
 * If must_use_low, then we have to insert at FD = low_fd.  o/w we start looking
 * for empty slots at low_fd. */
int insert_obj_fdt(struct fd_table *fdt, void *obj, int low_fd, int fd_flags,
                   bool must_use_low)
{
	int slot;

	spin_lock(&fdt->lock);
	slot = __get_fd(fdt, low_fd, must_use_low);
	if (slot < 0) {
		spin_unlock(&fdt->lock);
		return slot;
	}
	assert(slot < fdt->max_files &&
	       fdt->fd[slot].fd_chan == 0);
	chan_incref((struct chan*)obj);
	fdt->fd[slot].fd_chan = obj;
	fdt->fd[slot].fd_flags = fd_flags;
	spin_unlock(&fdt->lock);
	return slot;
}

/* Closes all open files.  Mostly just a "put" for all files.  If cloexec, it
 * will only close the FDs with FD_CLOEXEC (opened with O_CLOEXEC or fcntld).
 *
 * Notes on concurrency:
 * - Can't hold spinlocks while we call cclose, since it might sleep eventually.
 * - We're called from proc_destroy, so we could have concurrent openers trying
 *   to add to the group (other syscalls), hence the "closed" flag.
 * - dot and slash chans are dealt with in proc_free.  its difficult to close
 *   and zero those with concurrent syscalls, since those are a source of krefs.
 * - Once we lock and set closed, no further additions can happen.  To simplify
 *   our closes, we also allow multiple calls to this func (though that should
 *   never happen with the current code). */
void close_fdt(struct fd_table *fdt, bool cloexec)
{
	struct chan *chan;
	struct file_desc *to_close;
	int idx = 0;

	to_close = kzmalloc(sizeof(struct file_desc) * fdt->max_files,
	                    MEM_WAIT);
	spin_lock(&fdt->lock);
	if (fdt->closed) {
		spin_unlock(&fdt->lock);
		kfree(to_close);
		return;
	}
	for (int i = 0; i < fdt->max_fdset; i++) {
		if (GET_BITMASK_BIT(fdt->open_fds->fds_bits, i)) {
			/* while max_files and max_fdset might not line up, we should never
			 * have a valid fdset higher than files */
			assert(i < fdt->max_files);
			if (cloexec && !(fdt->fd[i].fd_flags & FD_CLOEXEC))
				continue;
			chan = fdt->fd[i].fd_chan;
			to_close[idx].fd_tap = fdt->fd[i].fd_tap;
			fdt->fd[i].fd_tap = 0;
			fdt->fd[i].fd_chan = 0;
			to_close[idx++].fd_chan = chan;
			CLR_BITMASK_BIT(fdt->open_fds->fds_bits, i);
		}
	}
	/* it's just a hint, we can build back up from being 0 */
	fdt->hint_min_fd = 0;
	if (!cloexec) {
		free_fd_set(fdt);
		fdt->closed = TRUE;
	}
	spin_unlock(&fdt->lock);
	/* We go through some hoops to close/decref outside the lock.  Nice for not
	 * holding the lock for a while; critical in case the decref/cclose sleeps
	 * (it can) */
	for (int i = 0; i < idx; i++) {
		cclose(to_close[i].fd_chan);
		if (to_close[i].fd_tap)
			kref_put(&to_close[i].fd_tap->kref);
	}
	kfree(to_close);
}

/* Inserts all of the files from src into dst, used by sys_fork(). */
void clone_fdt(struct fd_table *src, struct fd_table *dst)
{
	struct chan *chan;
	int ret;

	spin_lock(&src->lock);
	if (src->closed) {
		spin_unlock(&src->lock);
		return;
	}
	spin_lock(&dst->lock);
	if (dst->closed) {
		warn("Destination closed before it opened");
		spin_unlock(&dst->lock);
		spin_unlock(&src->lock);
		return;
	}
	while (src->max_files > dst->max_files) {
		ret = grow_fd_set(dst);
		if (ret < 0) {
			set_error(-ret, "Failed to grow for a clone_fdt");
			spin_unlock(&dst->lock);
			spin_unlock(&src->lock);
			return;
		}
	}
	for (int i = 0; i < src->max_fdset; i++) {
		if (GET_BITMASK_BIT(src->open_fds->fds_bits, i)) {
			/* while max_files and max_fdset might not line up, we should never
			 * have a valid fdset higher than files */
			assert(i < src->max_files);
			chan = src->fd[i].fd_chan;
			assert(i < dst->max_files && dst->fd[i].fd_chan == 0);
			SET_BITMASK_BIT(dst->open_fds->fds_bits, i);
			dst->fd[i].fd_chan = chan;
			chan_incref(chan);
		}
	}
	dst->hint_min_fd = src->hint_min_fd;
	spin_unlock(&dst->lock);
	spin_unlock(&src->lock);
}
