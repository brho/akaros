/* Copyright (c) 2015 Google Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * User FDs
 *
 * There are a bunch of Linux APIs that we can implement in userspace that use
 * FDs as a handle.  Eventually that handle gets passed to close().  User FDs
 * are a chunk of reserved numbers in the space of FDs, used by various
 * userspace libraries that use FDs as their API.
 *
 * The user FD space starts where the kernel's leaves off.  Currently, the
 * kernel claims 19 bits of the 32 bit int for an FD.  The MSB flags whether it
 * is negative or not.  That leaves 12 bits for us. */

#include <sys/user_fd.h>
#include <parlib/arch/atomic.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

static struct user_fd **ufds = 0;
static size_t nr_ufds = 0;

/* Finds a free user FD and stores ufd there.  Returns the FD, or -1 on error
 * (out of FDs).  You can remove it with a close(). */
int ufd_get_fd(struct user_fd *ufd)
{
	struct user_fd **new_ufds;
	int fd;

	if (!ufds) {
		nr_ufds = 1 << (sizeof(int) * 8 - LOG2_UP(NR_FILE_DESC_MAX)
				- 1);
		/* Two things: instead of worrying about growing and reallocing
		 * (which would need a lock), let's just alloc the entire 2^15
		 * bytes (32KB).  Also, it's unlikely, but we might have two
		 * threads trying to init at once.  First one wins, second one
		 * aborts (and frees). */
		new_ufds = malloc(sizeof(struct user_fd*) * nr_ufds);
		memset(new_ufds, 0, sizeof(struct user_fd*) * nr_ufds);
		if (!atomic_cas_ptr((void**)&ufds, 0, new_ufds))
			free(new_ufds);
		cmb();
	}
	/* At this point, ufds is set.  Just do a linear search for an empty
	 * slot.  We're not actually bound to return the lowest number
	 * available, so in the future we could do things like partition the
	 * space based on vcoreid so we start in different areas, or maintain a
	 * 'last used' hint FD. */
	for (int i = 0; i < nr_ufds; i++) {
		if (!ufds[i]) {
			if (atomic_cas_ptr((void**)&ufds[i], 0, ufd)) {
				fd = i + USER_FD_BASE;
				ufds[i]->fd = fd;
				return fd;
			}
		}
	}
	__set_errno(ENFILE);
	return -1;
}

/* Given an FD, returns the user_fd struct.  Returns 0 and sets errno if there's
 * an error.  There's no protection for concurrent closes, just like how you
 * shouldn't attempt to use an FD after closing.  So don't do stuff like:
 * 	foo = ufd_lookup(7);
 * 	close(7);
 * 	foo->whatever = 6; // foo could be free!
 *
 * or
 * 	close(7);
 * 	foo = ufd_lookup(7);
 * 	// this might succeed if it races with close()
 * 	foo->whatever = 6; // foo could be free!
 */
struct user_fd *ufd_lookup(int fd)
{
	if (!ufds || (fd - USER_FD_BASE >= nr_ufds)) {
		__set_errno(EBADF);
		return 0;
	}
	return ufds[fd - USER_FD_BASE];
}

/* Removes the user_fd from the FD space, calls its callback.  Returns 0 on
 * success.  -1 and sets errno o/w (e.g. EBADF). */
int glibc_close_helper(int fd)
{
	struct user_fd *ufd = ufd_lookup(fd);
	if (!ufd)
		return -1;
	ufds[fd - USER_FD_BASE] = 0;
	ufd->close(ufd);
	return 0;
}
