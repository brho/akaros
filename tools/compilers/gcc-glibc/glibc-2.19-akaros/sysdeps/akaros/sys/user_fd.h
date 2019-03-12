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

#ifndef _GLIBC_AKAROS_SYS_USER_FD_H
#define _GLIBC_AKAROS_SYS_USER_FD_H

#include <ros/limits.h>

/* NR_FILE_DESC_MAX marks the limit of the kernel's FDs */
#define USER_FD_BASE NR_FILE_DESC_MAX

/* Tracker for user FDs.  Clients embed this in one of their structs.  When
 * someone calls glibc's close() on the FD, it'll call the close func ptr.  The
 * callback uses container_of() to get its object.
 *
 * 'magic' can be whatever the client wants - it could be useful to make sure
 * you have the right type of FD. */
struct user_fd {
	int				magic;
	int				fd;
	void (*close)(struct user_fd *);
};

/* Finds a free user FD and stores ufd there.  Returns the FD, or -1 on error
 * (out of FDs).  You can remove it with a close(). */
int ufd_get_fd(struct user_fd *ufd);

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
 *      // this might succeed if it races with close()
 * 	foo->whatever = 6; // foo could be free!
 */
struct user_fd *ufd_lookup(int fd);

/* Called by glibc's close.  Do not call this directly. */
int glibc_close_helper(int fd);

#endif /* _GLIBC_AKAROS_SYS_USER_FD_H */
