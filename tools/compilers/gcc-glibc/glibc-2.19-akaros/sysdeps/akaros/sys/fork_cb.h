/* Copyright (c) 2016 Google Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * fork() callbacks.
 *
 * These are called after a process forks by the child.
 *
 * To register a cb, do your own allocation of a fork_cb, fill in func, then
 * call register_fork_cb.  You cannot remove your CB.  Concurrent calls to
 * fork() may or may not run your callback.
 *
 * Due to the plethora of user-level implementations of things normally in the
 * kernel, such as select(), those systems may need to deal with fork() in a
 * special way.  Imagine a process that forks, but does not exec.  The state of
 * those systems may no longer be valid, since they may rely on kernel state
 * that is no longer present.
 *
 * Specifically, select is built in taps, and select is supposed to be
 * stateless, but ours relies on kernel state (taps).  Taps are not inherited
 * (via fork or dup).  If you forked, select() would think it had taps set up,
 * when in fact it didn't. */

#pragma once

#include <sys/types.h>

/* 2LSs need to set these CBs if they want to be able to fork. */
extern void (*pre_fork_2ls)(void);
extern void (*post_fork_2ls)(pid_t ret);

struct fork_cb {
	struct fork_cb				*next;
	void (*func)(void);
};

extern struct fork_cb *fork_callbacks;	/* for use within glibc */

void register_fork_cb(struct fork_cb *cb);
