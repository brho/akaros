/* Copyright (c) 2016 Google Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * poll(), implemented on top of our super-spurious select().
 *
 * It's a little backwards to do poll() on select(), but both are pretty lousy
 * and purely for compatibility on Akaros.  For those that use poll, but not
 * select or epoll, this one's for you.
 *
 * All the caveats from our select() apply to poll().
 *
 * Additionally, we won't return POLLNVAL if an FD isn't open.  select() will
 * just fail, and we'll return the error.  If anyone has a program that actually
 * needs that behavior, we can revisit this.
 *
 * We won't implicitly track errors like POLLHUP if you also don't ask for at
 * least POLLIN or POLLOUT.  If try to poll for errors only, you'll get nothing.
 * Likewise, if there is an error/HUP, you'll wake up, but it'll look like a
 * read/write is ready.  (Same with select).  You'll notice when you go to
 * actually read() or write() later, which is pretty much mandatory for this
 * version of poll(). */

#define _GNU_SOURCE
#include <poll.h>
#include <sys/select.h>
#include <unistd.h>

int poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
	struct timespec local_ts, *ts_timeout = 0;

	if (timeout >= 0) {
		ts_timeout = &local_ts;
		ts_timeout->tv_sec = timeout / 1000;
		ts_timeout->tv_nsec = (timeout % 1000) * 1000000;
	}
	return ppoll(fds, nfds, ts_timeout, 0);
}

int ppoll(struct pollfd *fds, nfds_t nfds, const struct timespec *timeout_ts,
          const sigset_t *sigmask)
{
	int max_fd_plus_one = 0;
	fd_set rd_fds, wr_fds, ex_fds;
	int ret;

	FD_ZERO(&rd_fds);
	FD_ZERO(&wr_fds);
	FD_ZERO(&ex_fds);
	for (int i = 0; i < nfds; i++) {
		if (fds[i].fd == -1)
			continue;
		if (max_fd_plus_one < fds[i].fd + 1)
			max_fd_plus_one = fds[i].fd + 1;
		if (fds[i].events & (POLLIN | POLLPRI))
			FD_SET(i, &rd_fds);
		if (fds[i].events & POLLOUT)
			FD_SET(i, &wr_fds);
		/* TODO: We should be also asking for exceptions on all FDs.  But select
		 * is spurious, so it will actually tell us we had errors on all of our
		 * FDs, which will probably confuse programs. */
	}
	ret = pselect(max_fd_plus_one, &rd_fds, &wr_fds, &ex_fds, timeout_ts,
	              sigmask);
	if (ret <= 0)
		return ret;
	ret = 0;
	for (int i = 0; i < nfds; i++) {
		if (fds[i].fd == -1)
			continue;
		ret++;
		fds[i].revents = 0;
		if (FD_ISSET(i, &rd_fds))
			fds[i].revents |= POLLIN | POLLPRI;
		if (FD_ISSET(i, &wr_fds))
			fds[i].revents |= POLLOUT;
		if (FD_ISSET(i, &ex_fds))
			fds[i].revents |= POLLERR | POLLHUP;
	}
	return ret;
}
