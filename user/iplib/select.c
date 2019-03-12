/* Copyright (c) 2016 Google Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * select()
 *
 * Our select() is a bit rough and only works with FDs where fstat can return
 * S_READABLE or S_WRITABLE.  For the most part, this applies to qio queues,
 * which are the basis for a lot of the network stack and pipes.  FDs where
 * fstat doesn't tell us the readiness will have races.
 *
 * Under the hood, our select() is implemented with epoll (and under that, FD
 * taps).  Those can only detect edges (e.g. a socket becomes readable).
 *
 * The problem is that we want to detect a level status (e.g. socket is
 * readable) with an edge event (e.g. socket *becomes* readable).  To do this,
 * when someone initially selects, the FD gets tracked with epoll and we
 * manually poll the FDs with fstat.  Subsequent selects() will still be tracked
 * in the epoll set, but since apps can select() even on FDs they didn't drain
 * to the point of blocking, we still need to poll every FD on every select()
 * call.
 *
 * We maintain one FD set per program.  It tracks *any* FD being tracked by
 * *any* select call.  This is because you can only have one tap per FD.
 * Regardless of whether the user asked for read/write/except, the FD gets
 * watched for anything until it closes.
 *
 * One issue with the global FD set is that one thread may consume the epoll
 * events intended for another thread (or even for itself at another call
 * site!).  To get around this, only one thread is the actual epoller, and the
 * others block on a mutex.  TLS isn't an option for two reasons: not all 2LSs
 * use TLS (minor concern, maybe they should) and there are some threads who
 * make multiple select calls - we actually want per-call-site-and-thread fd
 * sets.
 *
 * Notes:
 * - pselect might be racy
 * - if the user has no read/write/except sets, we won't wait.  some users of
 *   select use it as a timer only.  if that comes up, we can expand this.
 * - if you epoll or FD tap an FD, then try to use select on it, you'll get an
 *   error (only one tap per FD).  select() only knows about the FDs in its set.
 * - if you select() on a readfd that is a disk file, it'll always say it is
 *   available for I/O.
 */

#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <errno.h>
#include <malloc.h>
#include <parlib/arch/arch.h>
#include <parlib/uthread.h>
#include <parlib/parlib.h>
#include <ros/common.h>
#include <ros/fs.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/close_cb.h>
#include <sys/epoll.h>
#include <sys/fork_cb.h>

static int epoll_fd;
static fd_set all_fds;
static fd_set working_read_fds;
static fd_set working_write_fds;
static fd_set working_except_fds;
static uth_mutex_t *epoll_mtx;

static bool fd_is_set(unsigned int fd, fd_set *set)
{
	if (fd > FD_SETSIZE)
		return FALSE;
	if (!set)
		return FALSE;
	return FD_ISSET(fd, set);
}

static void select_fd_closed(int fd)
{
	/* Slightly racy, but anything concurrently added will be closed later,
	 * and after it is_set. */
	if (!fd_is_set(fd, &all_fds))
		return;
	/* We just need to stop tracking FD.  We do not need to remove it from
	 * the epoll set, since that will happen automatically on close(). */
	uth_mutex_lock(epoll_mtx);
	FD_CLR(fd, &all_fds);
	uth_mutex_unlock(epoll_mtx);
}

static void select_forked(void)
{
	struct epoll_event ep_ev;

	uth_mutex_lock(epoll_mtx);
	for (int i = 0; i < FD_SETSIZE; i++) {
		if (fd_is_set(i, &all_fds)) {
			ep_ev.events = EPOLLET | EPOLLIN | EPOLLOUT | EPOLLHUP |
				       EPOLLERR;
			ep_ev.data.fd = i;
			/* Discard error.  The underlying tap is gone, and the
			 * epoll ctlr might also have been emptied.  We just
			 * want to make sure there is no epoll/tap so that a
			 * future CTL_ADD doesn't fail. */
			epoll_ctl(epoll_fd, EPOLL_CTL_DEL, i, &ep_ev);
			FD_CLR(i, &all_fds);
		}
	}
	uth_mutex_unlock(epoll_mtx);
}

static void select_init(void *arg)
{
	static struct close_cb select_close_cb = {.func = select_fd_closed};
	static struct fork_cb select_fork_cb = {.func = select_forked};

	register_close_cb(&select_close_cb);
	epoll_fd = epoll_create(FD_SETSIZE);
	if (epoll_fd < 0) {
		perror("select failed epoll_create");
		exit(-1);
	}
	epoll_mtx = uth_mutex_alloc();
	register_fork_cb(&select_fork_cb);
}

static int select_tv_to_ep_timeout(struct timeval *tv)
{
	if (!tv)
		return -1;
	return tv->tv_sec * 1000 + DIV_ROUND_UP(tv->tv_usec, 1000);
}

/* Helper: check with the kernel if FD is readable/writable or not.  Some apps
 * will call select() on something even if it is already actionable, and not
 * wait until they get the EAGAIN.
 *
 * This modifies the global working_ fd sets by setting bits of actionable FDs
 * and will return the number of bits turned on.  So basically, 1 for readable
 * xor writable, 2 for both.
 *
 * TODO: this *won't* work for disk based files.  It only works on qids that are
 * backed with qio queues or something similar, where the device has support for
 * setting DMREADABLE/DMWRITABLE. */
static unsigned int fd_set_actionable(int fd, fd_set *readfds, fd_set *writefds)
{
	struct stat stat_buf;
	int ret;

	/* Avoid the stat call on FDs we're not tracking (which should trigger
	 * an error, or give us the stat for FD 0). */
	if (!(fd_is_set(fd, readfds) || fd_is_set(fd, writefds)))
		return 0;
	ret = fstat(fd, &stat_buf);
	assert(!ret);
	ret = 0;
	if (fd_is_set(fd, readfds)) {
		if (S_READABLE(stat_buf.st_mode)) {
			ret++;
			FD_SET(fd, &working_read_fds);
		}
	}
	if (fd_is_set(fd, writefds)) {
		if (S_WRITABLE(stat_buf.st_mode)) {
			ret++;
			FD_SET(fd, &working_write_fds);
		}
	}
	return ret;
}

/* Helper: extracts events from ep_result for types ep_event_types, and sets
 * their bits in ret_fds if the FD was watched.  Returns the number of bits set.
 */
static int extract_bits_for_events(struct epoll_event *ep_result,
                                   uint32_t ep_event_types,
                                   fd_set *watched_fds, fd_set *ret_fds)
{
	int ret = 0;
	int fd = ep_result->data.fd;

	if (ep_result->events & ep_event_types) {
		if (fd_is_set(fd, watched_fds) && !FD_ISSET(fd, ret_fds)) {
			FD_SET(fd, ret_fds);
			ret++;
		}
	}
	return ret;
}

int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
           struct timeval *timeout)
{
	struct epoll_event ep_ev;
	struct epoll_event *ep_results;
	int ret, ep_ret, ep_timeout;
	static parlib_once_t once = PARLIB_ONCE_INIT;
	struct timeval start_tv[1], end_tv[1];

	parlib_run_once(&once, select_init, NULL);
	/* good thing nfds is a signed int... */
	if (nfds < 0) {
		errno = EINVAL;
		return -1;
	}
loop:
	if (timeout)
		gettimeofday(start_tv, NULL);
	ep_timeout = select_tv_to_ep_timeout(timeout);
	uth_mutex_lock(epoll_mtx);
	for (int i = 0; i < nfds; i++) {
		if ((fd_is_set(i, readfds) || fd_is_set(i, writefds) ||
		     fd_is_set(i, exceptfds)) &&
		    !fd_is_set(i, &all_fds)) {

			FD_SET(i, &all_fds);
			/* FDs that we track for *any* reason with select will
			 * be tracked for *all* reasons with epoll. */
			ep_ev.events = EPOLLET | EPOLLIN | EPOLLOUT | EPOLLHUP |
				       EPOLLERR;
			ep_ev.data.fd = i;
			if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, i, &ep_ev)) {
				/* We might have failed because we tried to set
				 * up too many FD tap types.  Listen FDs, for
				 * instance, can only be tapped for READABLE and
				 * HANGUP.  Let's try for one of those. */
				if (errno == ENOSYS) {
					ep_ev.events = EPOLLET | EPOLLIN |
						       EPOLLHUP;
					if (!epoll_ctl(epoll_fd, EPOLL_CTL_ADD,
						       i, &ep_ev))
						continue;
				}
				/* Careful to unlock before calling perror.
				 * perror calls close, which calls our CB, which
				 * grabs the lock. */
				uth_mutex_unlock(epoll_mtx);
				perror("select epoll_ctl failed");
				return -1;
			}
		}
	}
	/* Since we just added some FDs to our tracking set, we don't know if
	 * they are readable or not.  We'll only catch edge-triggered changes in
	 * the future.
	 *
	 * Similarly, it is legal to select on a readable FD even if you didn't
	 * consume all of the data yet; similarly for writers on non-full FDs.
	 *
	 * Additionally, since there is a global epoll set, we could have
	 * multiple threads epolling concurrently and one thread could consume
	 * the events that should wake another thread.  Also, keep in mind we
	 * could also have a single thread that selects multiple times on
	 * separate FD sets.
	 *
	 * Due to any of these cases, we need to check every FD this select call
	 * cares about (i.e. in an fd_set) to see if it is actionable.  We do it
	 * while holding the mutex to prevent other threads from consuming our
	 * epoll events. */
	ret = 0;
	FD_ZERO(&working_read_fds);
	FD_ZERO(&working_write_fds);
	FD_ZERO(&working_except_fds);
	/* Note the helper sets bits in the working_ fd sets */
	for (int i = 0; i < nfds; i++)
		ret += fd_set_actionable(i, readfds, writefds);
	if (ret) {
		if (readfds)
			*readfds = working_read_fds;
		if (writefds)
			*writefds = working_write_fds;
		uth_mutex_unlock(epoll_mtx);
		return ret;
	}
	/* Need to check for up to FD_SETSIZE - nfds isn't the size of all FDs
	 * tracked; it's the size of only our current select call */
	ep_results = malloc(sizeof(struct epoll_event) * FD_SETSIZE);
	if (!ep_results) {
		uth_mutex_unlock(epoll_mtx);
		errno = ENOMEM;
		return -1;
	}
	ep_ret = epoll_wait(epoll_fd, ep_results, FD_SETSIZE, ep_timeout);
	/* We need to hold the mtx during all of this processing since we're
	 * using the global working_ fds sets.  We can't modify the
	 * readfds/writefds/exceptfds until we're sure we are done. */
	ret = 0;
	/* Note that ret can be > ep_ret.  An FD that is both readable and
	 * writable counts as one event for epoll, but as two bits for select.
	 * */
	for (int i = 0; i < ep_ret; i++) {
		ret += extract_bits_for_events(&ep_results[i],
					       EPOLLIN | EPOLLHUP,
		                               readfds, &working_read_fds);
		ret += extract_bits_for_events(&ep_results[i],
					       EPOLLOUT | EPOLLHUP,
		                               writefds, &working_write_fds);
		ret += extract_bits_for_events(&ep_results[i], EPOLLERR,
		                               exceptfds, &working_except_fds);
	}
	free(ep_results);
	if (ret) {
		if (readfds)
			*readfds = working_read_fds;
		if (writefds)
			*writefds = working_write_fds;
		if (exceptfds)
			*exceptfds = working_except_fds;
	}
	uth_mutex_unlock(epoll_mtx);
	/* TODO: Consider updating timeval for non-timeouts.  It's not mandatory
	 * (POSIX). */
	if (ret)
		return ret;
	/* If we have no rets at this point, there are a few options: we could
	 * have timed out (if requested), or we could have consumed someone
	 * else's event.  No one could have consumed our event, since we were
	 * the only epoller (while holding the mtx).  In the latter case, we'll
	 * need to try again, but with an updated timeout. */
	if (timeout) {
		gettimeofday(end_tv, NULL);
		timersub(end_tv, start_tv, end_tv);	/* diff in end_tv */
		if (timercmp(timeout, end_tv, >))
			timersub(timeout, end_tv, timeout);
		else
			return 0;	/* select timed out */
	}
	goto loop;
}

int pselect(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
            const struct timespec *timeout, const sigset_t *sigmask)
{
	int ready;
	sigset_t origmask;
	struct timeval local_tv, *tv = &local_tv;

	if (!timeout) {
		tv = 0;
	} else {
		tv->tv_sec = timeout->tv_sec;
		tv->tv_usec = DIV_ROUND_UP(timeout->tv_nsec, 1000);
	}
	/* TODO: this is probably racy */
	sigprocmask(SIG_SETMASK, sigmask, &origmask);
	ready = select(nfds, readfds, writefds, exceptfds, tv);
	sigprocmask(SIG_SETMASK, &origmask, NULL);
	return ready;
}
