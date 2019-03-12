/* Copyright (c) 2015 Google Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * close() callbacks.
 *
 * Akaros has various utilities in userspace that operate on FDs, but those
 * facilities are in the kernel in other OSs.  Examples include epoll and the
 * Rocks (socket layer).  So far, these are only for compatibility.
 *
 * These facilities would like to know when the FD is closed.  In lieu of
 * encoding the callback info in the FD, close() will call all of the registered
 * callbacks for every FD that closes.  That's not ideal, so these facilities
 * should only register their FD if a program actually uses the facility.
 *
 * To register a cb, do your own allocation of a close_cb, fill in func, then
 * call register_close_cb.  You cannot remove your CB.  Concurrent calls to
 * close() may or may not run your callback.  Do not hand out an FD to a user
 * until you have registered your CB. */

#pragma once

struct close_cb {
	struct close_cb *next;
	void (*func)(int fd);
};

extern struct close_cb *close_callbacks;	/* for use within glibc */

void register_close_cb(struct close_cb *cb);
