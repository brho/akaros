/* Copyright (c) 2017 Google, Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details. */

#define _GNU_SOURCE
#include <utest/utest.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>


TEST_SUITE("QIO");

/* <--- Begin definition of test cases ---> */

static bool fd_is_writable(int fd)
{
	struct stat stat_buf;
	int ret;

	ret = fstat(fd, &stat_buf);
	UT_ASSERT_FMT("fstat failed", !ret);
	return S_WRITABLE(stat_buf.st_mode);
}

/* This is a little fragile, in that it relies on the kernel's value of
 * Maxatomic in qio.c. */
bool test_partial_write_to_full_queue(void)
{
	int pipefd[2];
	ssize_t ret;
	char *buf;
	size_t buf_sz = 1ULL << 20;

	buf = malloc(buf_sz);
	UT_ASSERT_FMT("buf alloc failed", buf);
	ret = pipe2(pipefd, O_NONBLOCK);
	UT_ASSERT_FMT("pipe2 failed", ret == 0);

	/* Fill the pipe.  The limit is usually O(10K) */
	do {
		ret = write(pipefd[1], buf, 1024);
	} while (ret > 0);
	UT_ASSERT_FMT("Didn't get EAGAIN, got %d", errno == EAGAIN, errno);

	/* The way qio works is we accept the last block, and won't accept
	 * future blocks until we drop below the limit.  Let's drain until we
	 * get there.  Should be only one or two. */
	while (!fd_is_writable(pipefd[1])) {
		ret = read(pipefd[0], buf, 1024);
		UT_ASSERT_FMT("Failed to read from pipe with data", ret > 0);
	}

	/* Now we have a little room.  If we send in a very large write, greater
	 * than Maxatomic, we should get a partial write.  If we get -1 back,
	 * then the kernel said it was writable, but we couldn't write.  This
	 * happened once when the kernel would write some, then get EAGAIN and
	 * throw - ignoring the successful initial write. */
	ret = write(pipefd[1], buf, buf_sz);
	UT_ASSERT_FMT("write error %d, errno %d", ret > 0, ret, errno);
	UT_ASSERT_FMT("wrote %d >= %d buf_sz", ret < buf_sz, ret, buf_sz);

	free(buf);
	close(pipefd[0]);
	close(pipefd[1]);
	return TRUE;
}


/* <--- End definition of test cases ---> */

struct utest utests[] = {
	UTEST_REG(partial_write_to_full_queue),
};
int num_utests = sizeof(utests) / sizeof(struct utest);

int main(int argc, char *argv[])
{
	char **whitelist = &argv[1];
	int whitelist_len = argc - 1;

	RUN_TEST_SUITE(utests, num_utests, whitelist, whitelist_len);
}
