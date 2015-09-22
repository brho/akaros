/* Copyright (c) 2015 Google Inc
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * #eventfd test, using the glibc interface mostly. */

#include <stdlib.h>
#include <stdio.h>
#include <parlib/parlib.h>
#include <unistd.h>
#include <pthread.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/eventfd.h>
#include <sys/epoll.h>


#define handle_error(msg) \
        do { perror(msg); exit(-1); } while (0)

static void epoll_on_efd(int efd)
{
	#define EP_SET_SZ 10	/* this is actually the ID of the largest FD */
	int epfd = epoll_create(EP_SET_SZ);
	struct epoll_event ep_ev;
	struct epoll_event results[EP_SET_SZ];

	if (epfd < 0)
		handle_error("epoll_create");
	ep_ev.events = EPOLLIN | EPOLLET;
	ep_ev.data.fd = efd;
	if (epoll_ctl(epfd, EPOLL_CTL_ADD, efd, &ep_ev))
		handle_error("epoll_ctl_add eventfd");
	if (epoll_wait(epfd, results, EP_SET_SZ, -1) != 1)
		handle_error("epoll_wait");
	close(epfd);
}

static pthread_attr_t pth_attrs;
static bool upped;

static void *upper_thread(void *arg)
{
	int efd = (int)(long)arg;
	uthread_sleep(1);
	if (eventfd_write(efd, 1))
		handle_error("upper write");
	upped = TRUE;
	return 0;
}

int main(int argc, char **argv)
{
	int ret;
	eventfd_t efd_val = 0;
	int efd;
	pthread_t child;

	parlib_wants_to_be_mcp = FALSE; /* Make us an SCP with a 2LS */
	pthread_attr_init(&pth_attrs);
	if (pthread_attr_setdetachstate(&pth_attrs, PTHREAD_CREATE_DETACHED))
		handle_error("pth attrs");

	/* Semaphore counter, nonblocking */
	efd = eventfd(2, EFD_SEMAPHORE | EFD_NONBLOCK);
	if (efd < 0)
		handle_error("open sem");
	if (eventfd_read(efd, &efd_val))
		handle_error("first read");
	assert(efd_val == 1);
	if (eventfd_read(efd, &efd_val))
		handle_error("second read");
	assert(efd_val == 1);	/* always get 1 back from the SEM */
	ret = eventfd_read(efd, &efd_val);
	if ((ret != -1) && (errno != EAGAIN))
		handle_error("third read should be EAGAIN");

	if (pthread_create(&child, &pth_attrs, &upper_thread, (void*)(long)efd))
		handle_error("pth_create failed");
	epoll_on_efd(efd);
	if (eventfd_read(efd, &efd_val))
		handle_error("final read");
	assert(efd_val == 1);
	close(efd);

	/* Regular counter */
	efd = eventfd(2, 0);
	if (efd < 0)
		handle_error("open nonsem");
	if (eventfd_read(efd, &efd_val))
		handle_error("first read nonsem");
	assert(efd_val == 2);

	/* Will try to block in the kernel.  Using 'upped' to catch any quick
	 * returns.  It's not full-proof, but it can catch an O_NONBLOCK */
	if (pthread_create(&child, &pth_attrs, &upper_thread, (void*)(long)efd))
		handle_error("pth_create failed");
	upped = FALSE;
	if (eventfd_read(efd, &efd_val))
		handle_error("blocking read nonsem");
	cmb();
	assert(upped && efd_val == 1);

	/* Should still be 0.  Add 1 and then extract to see if it was. */
	if (eventfd_write(efd, 1))
		handle_error("write nonsem +1");
	if (eventfd_read(efd, &efd_val))
		handle_error("final read nonsem");
	/* 1 means it was 0 before we added 1.  it's 0 again, since we read. */
	assert(efd_val == 1);
	/* Test the max_val + 1 write */
	ret = eventfd_write(efd, -1);
	if ((ret != -1) && (errno != EINVAL))
		handle_error("write nonsem should have failed");
	if (eventfd_write(efd, 0xfffffffffffffffe))
		handle_error("largest legal write");
	close(efd);

	return 0;
}
