/* Copyright (c) 2013 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Reader-writer queue locks (sleeping locks).
 *
 * Readers favor readers; writers favor writers.  Check out rwlock.c for more
 * info.
 *
 * One consequence of this: "if some reader holds a rwlock, then any other
 * thread (including itself) can get an rlock". */

#pragma once

#include <ros/common.h>
#include <kthread.h>
#include <atomic.h>

struct rwlock {
	spinlock_t			lock;
	atomic_t			nr_readers;
	bool				writing;
	struct cond_var			readers;
	struct cond_var			writers;
};
typedef struct rwlock rwlock_t;

void rwinit(struct rwlock *rw_lock);
void rlock(struct rwlock *rw_lock);
bool canrlock(struct rwlock *rw_lock);
void runlock(struct rwlock *rw_lock);
void wlock(struct rwlock *rw_lock);
void wunlock(struct rwlock *rw_lock);
