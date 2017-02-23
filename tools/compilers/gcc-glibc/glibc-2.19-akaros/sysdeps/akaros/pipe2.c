/* Copyright (c) 2016 Google Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Pipes, now with openat! */

#include <errno.h>
#include <unistd.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

int __pipe2(int pipedes[2], int flags)
{
	int dirfd, dfd, d1fd, old_errno;
	char old_errstr[MAX_ERRSTR_LEN];

	if (!pipedes) {
		__set_errno(EFAULT);
		return -1;
	}
	dirfd = open("#pipe", O_PATH);
	if (dirfd < 0)
		return -1;
	dfd = openat(dirfd, "data", O_RDWR | flags);
	if (dfd < 0) {
		save_err(&old_errno, old_errstr);
		close(dirfd);
		restore_err(&old_errno, old_errstr);
		return -1;
	}
	d1fd = openat(dirfd, "data1", O_RDWR | flags);
	if (d1fd < 0) {
		save_err(&old_errno, old_errstr);
		close(dfd);
		close(dirfd);
		restore_err(&old_errno, old_errstr);
		return -1;
	}
	pipedes[0] = dfd;
	pipedes[1] = d1fd;
	close(dirfd);
	return 0;
}
weak_alias(__pipe2, pipe2)
