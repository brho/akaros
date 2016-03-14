/* Copyright (c) 2016 Google, Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details. */

/* Generic Uthread Mutexes.  2LSs implement their own methods, but we need a
 * 2LS-independent interface and default implementation. */

#include <parlib/uthread.h>
#include <sys/queue.h>
#include <parlib/spinlock.h>
#include <malloc.h>

struct uth_default_mtx;
struct uth_mtx_link {
	TAILQ_ENTRY(uth_mtx_link)	next;
	struct uth_default_mtx		*mtx;
	struct uthread				*uth;
};

struct uth_default_mtx {
	struct spin_pdr_lock		lock;
	TAILQ_HEAD(t, uth_mtx_link)	waiters;
	bool						locked;
};

static struct uth_default_mtx *uth_default_mtx_alloc(void)
{
	struct uth_default_mtx *mtx;

	mtx = malloc(sizeof(struct uth_default_mtx));
	assert(mtx);
	spin_pdr_init(&mtx->lock);
	TAILQ_INIT(&mtx->waiters);
	mtx->locked = FALSE;
	return mtx;
}

static void uth_default_mtx_free(struct uth_default_mtx *mtx)
{
	assert(TAILQ_EMPTY(&mtx->waiters));
	free(mtx);
}

static void __mutex_cb(struct uthread *uth, void *arg)
{
	struct uth_mtx_link *link = (struct uth_mtx_link*)arg;
	struct uth_default_mtx *mtx = link->mtx;

	/* We need to tell the 2LS that its thread blocked.  We need to do this
	 * before unlocking the mtx, since as soon as we unlock, the mtx could be
	 * released and our thread restarted.
	 *
	 * Also note the lock-ordering rule.  The mtx lock is grabbed before any
	 * locks the 2LS might grab. */
	uthread_has_blocked(uth, UTH_EXT_BLK_MUTEX);
	spin_pdr_unlock(&mtx->lock);
}

static void uth_default_mtx_lock(struct uth_default_mtx *mtx)
{
	struct uth_mtx_link link;

	spin_pdr_lock(&mtx->lock);
	if (!mtx->locked) {
		mtx->locked = TRUE;
		spin_pdr_unlock(&mtx->lock);
		return;
	}
	link.mtx = mtx;
	link.uth = current_uthread;
	TAILQ_INSERT_TAIL(&mtx->waiters, &link, next);
	/* the unlock is done in the yield callback.  as always, we need to do this
	 * part in vcore context, since as soon as we unlock the uthread could
	 * restart.  (atomically yield and unlock). */
	uthread_yield(TRUE, __mutex_cb, &link);
}

static void uth_default_mtx_unlock(struct uth_default_mtx *mtx)
{
	struct uth_mtx_link *first;

	spin_pdr_lock(&mtx->lock);
	first = TAILQ_FIRST(&mtx->waiters);
	if (first)
		TAILQ_REMOVE(&mtx->waiters, first, next);
	else
		mtx->locked = FALSE;
	spin_pdr_unlock(&mtx->lock);
	if (first)
		uthread_runnable(first->uth);
}

uth_mutex_t uth_mutex_alloc(void)
{
	if (sched_ops->mutex_alloc)
		return sched_ops->mutex_alloc();
	return (uth_mutex_t)uth_default_mtx_alloc();
}

void uth_mutex_free(uth_mutex_t m)
{
	if (sched_ops->mutex_free) {
		sched_ops->mutex_free(m);
		return;
	}
	uth_default_mtx_free((struct uth_default_mtx*)m);
}

void uth_mutex_lock(uth_mutex_t m)
{
	if (sched_ops->mutex_lock) {
		sched_ops->mutex_lock(m);
		return;
	}
	uth_default_mtx_lock((struct uth_default_mtx*)m);
}

void uth_mutex_unlock(uth_mutex_t m)
{
	if (sched_ops->mutex_unlock) {
		sched_ops->mutex_unlock(m);
		return;
	}
	uth_default_mtx_unlock((struct uth_default_mtx*)m);
}
