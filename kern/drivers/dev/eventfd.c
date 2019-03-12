/* Copyright (c) 2015 Google Inc
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * #eventfd device, the kernel-side implementation of man 2 eventfd.
 *
 * Unlike the Linux interface, which takes host-endian u64s, we read and write
 * strings.  It's a little slower, but it maintains the distributed-system
 * nature of Plan 9 devices. */

#include <ns.h>
#include <kmalloc.h>
#include <kref.h>
#include <atomic.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <error.h>
#include <sys/queue.h>
#include <fdtap.h>
#include <syscall.h>

struct dev efd_devtab;

static char *devname(void)
{
	return efd_devtab.name;
}

enum {
	Qdir,
	Qctl,
	Qefd,
};

static struct dirtab efd_dir[] = {
	{".", {Qdir, 0, QTDIR}, 0, DMDIR | 0555},
	{"ctl", {Qctl, 0, QTFILE}, 0, 0666},
	{"efd", {Qefd, 0, QTFILE}, 8, 0666},
};

enum {
	EFD_SEMAPHORE = 	1 << 0,
	EFD_MAX_VAL =		(unsigned long)(-2), // i.e. 0xfffffffffffffffe
};


struct eventfd {
	int 				flags;
	atomic_t			counter;
	struct fdtap_slist		fd_taps;
	spinlock_t			tap_lock;
	struct rendez			rv_readers;
	struct rendez			rv_writers;
	struct kref			refcnt;
};


static void efd_release(struct kref *kref)
{
	struct eventfd *efd = container_of(kref, struct eventfd, refcnt);

	/* All FDs with taps must be closed before we decreffed all the chans */
	assert(SLIST_EMPTY(&efd->fd_taps));
	kfree(efd);
}

static struct chan *efd_attach(char *spec)
{
	struct chan *c;
	struct eventfd *efd;

	c = devattach(devname(), spec);
	efd = kzmalloc(sizeof(struct eventfd), MEM_WAIT);
	SLIST_INIT(&efd->fd_taps);
	spinlock_init(&efd->tap_lock);
	rendez_init(&efd->rv_readers);
	rendez_init(&efd->rv_writers);
	/* Attach and walk are the two sources of chans.  Each returns a
	 * refcnt'd object, for the most part. */
	kref_init(&efd->refcnt, efd_release, 1);
	/* nothing special in the qid to ID this eventfd.  the main thing is the
	 * aux.  we could put a debugging ID in the path like pipe. */
	mkqid(&c->qid, Qdir, 0, QTDIR);
	c->aux = efd;
	/* just to be fancy and remove a syscall, if they pass spec == "sem",
	 * then we'll treat them as being in semaphore mode. */
	if (!strcmp(spec, "sem"))
		efd->flags |= EFD_SEMAPHORE;
	return c;
}

static struct walkqid *efd_walk(struct chan *c, struct chan *nc, char **name,
				unsigned int nname)
{
	struct walkqid *wq;
	struct eventfd *efd = c->aux;

	wq = devwalk(c, nc, name, nname, efd_dir, ARRAY_SIZE(efd_dir), devgen);
	/* Walk is a source of a distinct chan from this device.  The other
	 * source is attach.  Once created, these chans will eventually be
	 * closed, and when they close, they will decref their aux, efd.  All
	 * chans within this *instance* of eventfd share the same efd.  Each one
	 * will have one refcnt.  Each chan may also have several copies of its
	 * pointer out there (e.g. FD dup), all of which have their own *chan*
	 * refcnt.
	 *
	 * All of the above applies on successful walks that found all nname
	 * parts of the path.  A mid-success is wq: we got something.  wq->clone
	 * means we got to the end and the "big walk" considers this a success.
	 *
	 * There is a slight chance the new chan is the same as our original
	 * chan (if nc == c when we're called).  In which case, there's only one
	 * chan.  The number of refs on efd == the number of distinct chans
	 * within this instance of #eventfd. */
	if (wq != NULL && wq->clone != NULL && wq->clone != c)
		kref_get(&efd->refcnt, 1);
	return wq;
}

/* In the future, we could use stat / wstat to get and set O_NONBLOCK */
static size_t efd_stat(struct chan *c, uint8_t *db, size_t n)
{
	return devstat(c, db, n, efd_dir, ARRAY_SIZE(efd_dir), devgen);
}

static struct chan *efd_open(struct chan *c, int omode)
{
	return devopen(c, omode, efd_dir, ARRAY_SIZE(efd_dir), devgen);
}

static void efd_close(struct chan *c)
{
	struct eventfd *efd = c->aux;

	/* Here's where we put the ref from attach and successful walks */
	kref_put(&efd->refcnt);
}

static void efd_fire_taps(struct eventfd *efd, int filter)
{
	struct fd_tap *tap_i;

	if (SLIST_EMPTY(&efd->fd_taps))
		return;
	/* We're not expecting many FD taps, so it's not worth splitting readers
	 * from writers or anything like that.
	 * TODO: (RCU) Locking to protect the list and the tap's existence. */
	spin_lock(&efd->tap_lock);
	SLIST_FOREACH(tap_i, &efd->fd_taps, link)
		fire_tap(tap_i, filter);
	spin_unlock(&efd->tap_lock);
}

static int has_counts(void *arg)
{
	struct eventfd *efd = arg;

	return atomic_read(&efd->counter) != 0;
}

/* The heart of reading an eventfd */
static unsigned long efd_read_efd(struct eventfd *efd, struct chan *c)
{
	unsigned long old_count, new_count, ret;

	while (1) {
		old_count = atomic_read(&efd->counter);
		if (!old_count) {
			if (c->flag & O_NONBLOCK)
				error(EAGAIN, "Would block on #%s read",
				      devname());
			rendez_sleep(&efd->rv_readers, has_counts, efd);
		} else {
			if (efd->flags & EFD_SEMAPHORE) {
				new_count = old_count - 1;
				ret = 1;
			} else {
				new_count = 0;
				ret = old_count;
			}
			if (atomic_cas(&efd->counter, old_count, new_count))
				goto success;
		}
	}
success:
	rendez_wakeup(&efd->rv_writers);
	efd_fire_taps(efd, FDTAP_FILT_WRITABLE);
	return ret;
}

static size_t efd_read(struct chan *c, void *ubuf, size_t n, off64_t offset)
{
	struct eventfd *efd = c->aux;

	switch (c->qid.path) {
	case Qdir:
		return devdirread(c, ubuf, n, efd_dir, ARRAY_SIZE(efd_dir),
				  devgen);
	case Qctl:
		return readnum(offset, ubuf, n, efd->flags, NUMSIZE32);
	case Qefd:
		/* ignoring the chan offset for Qefd */
		return readnum(0, ubuf, n, efd_read_efd(efd, c), NUMSIZE64);
	default:
		panic("Bad Qid %p!", c->qid.path);
	}
	return -1;
}

static int has_room(void *arg)
{
	struct eventfd *efd = arg;
	return atomic_read(&efd->counter) != EFD_MAX_VAL;
}

/* The heart of writing an eventfd */
static void efd_write_efd(struct eventfd *efd, unsigned long add_to,
                          struct chan *c)
{
	unsigned long old_count, new_count;

	while (1) {
		old_count = atomic_read(&efd->counter);
		new_count = old_count + add_to;
		if (new_count > EFD_MAX_VAL) {
			if (c->flag & O_NONBLOCK)
				error(EAGAIN, "Would block on #%s write",
				      devname());
			rendez_sleep(&efd->rv_writers, has_room, efd);
		} else {
			if (atomic_cas(&efd->counter, old_count, new_count))
				goto success;
		}
	}
success:
	rendez_wakeup(&efd->rv_readers);
	efd_fire_taps(efd, FDTAP_FILT_READABLE);
}

static size_t efd_write(struct chan *c, void *ubuf, size_t n, off64_t offset)
{
	struct eventfd *efd = c->aux;
	unsigned long write_val;
	char num64[NUMSIZE64];

	switch (c->qid.path) {
	case Qctl:
		/* If we want to allow runtime changing of settings, we can do
		 * it here. */
		error(EFAIL, "No #%s ctl commands supported", devname());
		break;
	case Qefd:
		/* We want to give strtoul a null-terminated buf (can't handle
		 * arbitrary user strings).  Ignoring the chan offset too. */
		if (n > sizeof(num64))
			error(EAGAIN, "attempted to write %d chars, max %d", n,
				  sizeof(num64));
		memcpy(num64, ubuf, n);
		num64[n] = 0;	/* enforce trailing 0 */
		write_val = strtoul(num64, 0, 0);
		if (write_val == (unsigned long)(-1))
			error(EFAIL, "Eventfd write must not be -1");
		efd_write_efd(efd, write_val, c);
		break;
	default:
		panic("Bad Qid %p!", c->qid.path);
	}
	return n;
}

static char *efd_chaninfo(struct chan *c, char *ret, size_t ret_l)
{
	struct eventfd *efd = c->aux;

	snprintf(ret, ret_l, "QID type %s, flags %p, counter %p",
		 efd_dir[c->qid.path].name, efd->flags,
		 atomic_read(&efd->counter));
	return ret;
}

static int efd_tapfd(struct chan *c, struct fd_tap *tap, int cmd)
{
	struct eventfd *efd = c->aux;
	int ret;

	/* HANGUP, ERROR, and PRIORITY will never fire, but people can ask for
	 * them.  We don't actually support HANGUP, but epoll implies it.
	 * Linux's eventfd cand have ERROR, so apps can ask for it.  Likewise,
	 * priority is meaningless for us, but sometimes people ask for it. */
#define EFD_LEGAL_TAPS (FDTAP_FILT_READABLE | FDTAP_FILT_WRITABLE |        \
                        FDTAP_FILT_HANGUP | FDTAP_FILT_PRIORITY |          \
                        FDTAP_FILT_ERROR)

	switch (c->qid.path) {
	case Qefd:
		if (tap->filter & ~EFD_LEGAL_TAPS) {
			set_error(ENOSYS, "Unsupported #%s tap, must be %p",
				  devname(), EFD_LEGAL_TAPS);
			return -1;
		}
		spin_lock(&efd->tap_lock);
		switch (cmd) {
		case (FDTAP_CMD_ADD):
			SLIST_INSERT_HEAD(&efd->fd_taps, tap, link);
			ret = 0;
			break;
		case (FDTAP_CMD_REM):
			SLIST_REMOVE(&efd->fd_taps, tap, fd_tap, link);
			ret = 0;
			break;
		default:
			set_error(ENOSYS, "Unsupported #%s tap command %p",
				  devname(), cmd);
			ret = -1;
		}
		spin_unlock(&efd->tap_lock);
		return ret;
	default:
		set_error(ENOSYS, "Can't tap #%s file type %d", devname(),
		          c->qid.path);
		return -1;
	}
}

struct dev efd_devtab __devtab = {
	.name = "eventfd",
	.reset = devreset,
	.init = devinit,
	.shutdown = devshutdown,
	.attach = efd_attach,
	.walk = efd_walk,
	.stat = efd_stat,
	.open = efd_open,
	.create = devcreate,
	.close = efd_close,
	.read = efd_read,
	.bread = devbread,
	.write = efd_write,
	.bwrite = devbwrite,
	.remove = devremove,
	.wstat = devwstat,
	.power = devpower,
	.chaninfo = efd_chaninfo,
	.tapfd = efd_tapfd,
};
