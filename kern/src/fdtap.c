/* Copyright (c) 2015 Google Inc
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * FD taps.  Allows the user to receive events when certain things happen to an
 * FD's underlying device file/qid. */

#include <fdtap.h>
#include <event.h>
#include <kmalloc.h>
#include <syscall.h>
#include <error.h>
#include <umem.h>

static void tap_min_release(struct kref *kref)
{
	struct fd_tap *tap = container_of(kref, struct fd_tap, kref);

	cclose(tap->chan);
	kfree(tap);
}

static void tap_full_release(struct kref *kref)
{
	struct fd_tap *tap = container_of(kref, struct fd_tap, kref);

	devtab[tap->chan->type].tapfd(tap->chan, tap, FDTAP_CMD_REM);
	tap_min_release(kref);
}

/* Adds a tap with the file/qid of the underlying device for the requested FD.
 * The FD must be a chan, and the device must support the filter requested.
 *
 * Returns -1 or some other device-specific non-zero number on failure, 0 on
 * success. */
int add_fd_tap(struct proc *p, struct fd_tap_req *tap_req)
{
	struct fd_table *fdt = &p->open_files;
	struct fd_tap *tap;
	int ret = 0;
	struct chan *chan;
	int fd = tap_req->fd;

	if (fd < 0) {
		set_errno(EBADF);
		return -1;
	}
	tap = kzmalloc(sizeof(struct fd_tap), MEM_WAIT);
	tap->proc = p;
	tap->fd = fd;
	tap->filter = tap_req->filter;
	tap->ev_q = tap_req->ev_q;
	tap->ev_id = tap_req->ev_id;
	tap->data = tap_req->data;
	if (!is_user_rwaddr(tap->ev_q, sizeof(struct event_queue))) {
		set_error(EINVAL, "Tap request with bad event_queue %p",
			  tap->ev_q);
		kfree(tap);
		return -1;
	}
	spin_lock(&fdt->lock);
	if (fd >= fdt->max_fdset) {
		set_errno(ENFILE);
		goto out_with_lock;
	}
	if (!GET_BITMASK_BIT(fdt->open_fds->fds_bits, fd)) {
		set_errno(EBADF);
		goto out_with_lock;
	}
	if (!fdt->fd[fd].fd_chan) {
		set_error(EINVAL, "Can't tap a VFS file");
		goto out_with_lock;
	}
	chan = fdt->fd[fd].fd_chan;
	if (fdt->fd[fd].fd_tap) {
		set_error(EBUSY, "FD %d already has a tap", fd);
		goto out_with_lock;
	}
	if (!devtab[chan->type].tapfd) {
		set_error(ENOSYS, "Device %s does not handle taps",
				  devtab[chan->type].name);
		goto out_with_lock;
	}
	/* need to keep chan alive for our call to the device.  someone else
	 * could come in and close the FD and the chan, once we unlock */
	chan_incref(chan);
	tap->chan = chan;
	/* One for the FD table, one for us to keep the removal of *this* tap
	 * from happening until we've attempted to register with the device. */
	kref_init(&tap->kref, tap_full_release, 2);
	fdt->fd[fd].fd_tap = tap;
	/* As soon as we unlock, another thread can come in and remove our old
	 * tap from the table and decref it.  Our ref keeps us from removing it
	 * yet, as well as keeps the memory safe.  However, a new tap can be
	 * installed and registered with the device before we even attempt to
	 * register.  The devices should be able to handle multiple, distinct
	 * taps, even if they happen to have the same {proc, fd} tuple. */
	spin_unlock(&fdt->lock);
	/* For refcnting fans, the tap ref is weak/uncounted.  We'll protect the
	 * memory and call the device when tap is being released. */
	ret = devtab[chan->type].tapfd(chan, tap, FDTAP_CMD_ADD);
	if (ret) {
		/* we failed, so we need to make sure *our* tap is removed.  We
		 * haven't decreffed, so we know our tap pointer is unique. */
		spin_lock(&fdt->lock);
		if (fdt->fd[fd].fd_tap == tap) {
			fdt->fd[fd].fd_tap = 0;
			/* normally we can't decref a tap while holding a lock,
			 * but we know we have another reference so this won't
			 * trigger a release */
			kref_put(&tap->kref);
		}
		spin_unlock(&fdt->lock);
		/* Regardless of whether someone else removed it or not, *we*
		 * are the only ones that know that registration failed and that
		 * we shouldn't remove it.  Since we still hold a ref, we can
		 * change the release method to skip the device dereg. */
		tap->kref.release = tap_min_release;
	}
	kref_put(&tap->kref);
	return ret;
out_with_lock:
	spin_unlock(&fdt->lock);
	kfree(tap);
	return -1;
}

/* Removes the FD tap associated with FD.  Returns 0 on success, -1 with
 * errno/errstr on failure. */
int remove_fd_tap(struct proc *p, int fd)
{
	struct fd_table *fdt = &p->open_files;
	struct fd_tap *tap;

	spin_lock(&fdt->lock);
	tap = fdt->fd[fd].fd_tap;
	fdt->fd[fd].fd_tap = 0;
	spin_unlock(&fdt->lock);
	if (tap) {
		kref_put(&tap->kref);
		return 0;
	} else {
		set_error(EBADF, "FD %d was not tapped", fd);
		return -1;
	}
}

/* Fires off tap, with the events of filter having occurred.  Returns -1 on
 * error, though this need a little more thought.
 *
 * Some callers may require this to not block. */
int fire_tap(struct fd_tap *tap, int filter)
{
	ERRSTACK(1);
	struct event_msg ev_msg = {0};
	int fire_filt = tap->filter & filter;

	if (!fire_filt)
		return 0;
	if (waserror()) {
		/* The process owning the tap could trigger a kernel PF, as with
		 * any send_event() call.  Eventually we'll catch that with
		 * waserror. */
		warn("Tap for proc %d, fd %d, threw %s", tap->proc->pid,
		     tap->fd, current_errstr());
		poperror();
		return -1;
	}
	ev_msg.ev_type = tap->ev_id;	/* e.g. CEQ idx */
	ev_msg.ev_arg2 = fire_filt;	/* e.g. CEQ coalesce */
	ev_msg.ev_arg3 = tap->data;	/* e.g. CEQ data */
	send_event(tap->proc, tap->ev_q, &ev_msg, 0);
	poperror();
	return 0;
}
