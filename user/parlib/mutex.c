/* Copyright (c) 2016 Google, Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details. */

/* Generic Uthread Mutexes and CVs.  2LSs implement their own methods, but we
 * need a 2LS-independent interface and default implementation. */

#include <parlib/uthread.h>
#include <sys/queue.h>
#include <parlib/spinlock.h>
#include <malloc.h>

/* The linkage structs are for the yield callbacks */
struct uth_default_mtx;
struct uth_mtx_link {
	TAILQ_ENTRY(uth_mtx_link)	next;
	struct uth_default_mtx		*mtx;
	struct uthread				*uth;
};
TAILQ_HEAD(mtx_link_tq, uth_mtx_link);

struct uth_default_mtx {
	struct spin_pdr_lock		lock;
	struct mtx_link_tq			waiters;
	bool						locked;
};

struct uth_default_cv;
struct uth_cv_link {
	TAILQ_ENTRY(uth_cv_link)	next;
	struct uth_default_cv		*cv;
	struct uth_default_mtx		*mtx;
	struct uthread				*uth;
};
TAILQ_HEAD(cv_link_tq, uth_cv_link);

struct uth_default_cv {
	struct spin_pdr_lock		lock;
	struct cv_link_tq			waiters;
};


/************** Default Mutex Implementation **************/


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

static bool uth_default_mtx_trylock(struct uth_default_mtx *mtx)
{
	bool ret = FALSE;

	spin_pdr_lock(&mtx->lock);
	if (!mtx->locked) {
		mtx->locked = TRUE;
		ret = TRUE;
	}
	spin_pdr_unlock(&mtx->lock);
	return ret;
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


/************** Wrappers for the uthread mutex interface **************/


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

bool uth_mutex_trylock(uth_mutex_t m)
{
	if (sched_ops->mutex_trylock)
		return sched_ops->mutex_trylock(m);
	return uth_default_mtx_trylock((struct uth_default_mtx*)m);
}

void uth_mutex_unlock(uth_mutex_t m)
{
	if (sched_ops->mutex_unlock) {
		sched_ops->mutex_unlock(m);
		return;
	}
	uth_default_mtx_unlock((struct uth_default_mtx*)m);
}


/************** Recursive mutexes **************/

/* Note the interface type uth_recurse_mtx_t is a pointer to this struct. */
struct uth_recurse_mtx {
	uth_mutex_t					mtx;
	struct uthread				*lockholder;
	unsigned int				count;
};

uth_recurse_mutex_t uth_recurse_mutex_alloc(void)
{
	struct uth_recurse_mtx *r_mtx = malloc(sizeof(struct uth_recurse_mtx));

	assert(r_mtx);
	r_mtx->mtx = uth_mutex_alloc();
	r_mtx->lockholder = NULL;
	r_mtx->count = 0;
	return (uth_recurse_mutex_t)r_mtx;
}

void uth_recurse_mutex_free(uth_recurse_mutex_t r_m)
{
	struct uth_recurse_mtx *r_mtx = (struct uth_recurse_mtx*)r_m;

	uth_mutex_free(r_mtx->mtx);
	free(r_mtx);
}

void uth_recurse_mutex_lock(uth_recurse_mutex_t r_m)
{
	struct uth_recurse_mtx *r_mtx = (struct uth_recurse_mtx*)r_m;

	assert(!in_vcore_context());
	assert(current_uthread);
	/* We don't have to worry about races on current_uthread or count.  They are
	 * only written by the initial lockholder, and this check will only be true
	 * for the initial lockholder, which cannot concurrently call this function
	 * twice (a thread is single-threaded).
	 *
	 * A signal handler running for a thread should not attempt to grab a
	 * recursive mutex (that's probably a bug).  If we need to support that,
	 * we'll have to disable notifs temporarily. */
	if (r_mtx->lockholder == current_uthread) {
		r_mtx->count++;
		return;
	}
	uth_mutex_lock(r_mtx->mtx);
	r_mtx->lockholder = current_uthread;
	r_mtx->count = 1;
}

bool uth_recurse_mutex_trylock(uth_recurse_mutex_t r_m)
{
	struct uth_recurse_mtx *r_mtx = (struct uth_recurse_mtx*)r_m;
	bool ret;

	assert(!in_vcore_context());
	assert(current_uthread);
	if (r_mtx->lockholder == current_uthread) {
		r_mtx->count++;
		return TRUE;
	}
	ret = uth_mutex_trylock(r_mtx->mtx);
	if (ret) {
		r_mtx->lockholder = current_uthread;
		r_mtx->count = 1;
	}
	return ret;
}

void uth_recurse_mutex_unlock(uth_recurse_mutex_t r_m)
{
	struct uth_recurse_mtx *r_mtx = (struct uth_recurse_mtx*)r_m;

	r_mtx->count--;
	if (!r_mtx->count) {
		r_mtx->lockholder = NULL;
		uth_mutex_unlock(r_mtx->mtx);
	}
}


/************** Default Condition Variable Implementation **************/


static struct uth_default_cv *uth_default_cv_alloc(void)
{
	struct uth_default_cv *cv;

	cv = malloc(sizeof(struct uth_default_cv));
	assert(cv);
	spin_pdr_init(&cv->lock);
	TAILQ_INIT(&cv->waiters);
	return cv;
}

static void uth_default_cv_free(struct uth_default_cv *cv)
{
	assert(TAILQ_EMPTY(&cv->waiters));
	free(cv);
}

static void __cv_wait_cb(struct uthread *uth, void *arg)
{
	struct uth_cv_link *link = (struct uth_cv_link*)arg;
	struct uth_default_cv *cv = link->cv;
	struct uth_default_mtx *mtx = link->mtx;

	/* We need to tell the 2LS that its thread blocked.  We need to do this
	 * before unlocking the cv, since as soon as we unlock, the cv could be
	 * signalled and our thread restarted.
	 *
	 * Also note the lock-ordering rule.  The cv lock is grabbed before any
	 * locks the 2LS might grab. */
	uthread_has_blocked(uth, UTH_EXT_BLK_MUTEX);
	spin_pdr_unlock(&cv->lock);
	uth_mutex_unlock((uth_mutex_t)mtx);
}

/* Caller holds mtx.  We will 'atomically' release it and wait.  On return,
 * caller holds mtx again.  Once our uth is on the CV's list, we can release the
 * mtx without fear of missing a signal.
 *
 * POSIX refers to atomicity in this context as "atomically with respect to
 * access by another thread to the mutex and then the condition variable"
 *
 * The idea is that we hold the mutex to protect some invariant; we check it,
 * and decide to sleep.  Now we get on the list before releasing so that any
 * changes to that invariant (e.g. a flag is now TRUE) happen after we're on the
 * list, and so that we don't miss the signal.  To be more clear, the invariant
 * in a basic wake-up flag scenario is: "whenever a flag is set from FALSE to
 * TRUE, all waiters that saw FALSE are on the CV's waitqueue."  The mutex is
 * required for this invariant.
 *
 * Note that signal/broadcasters do not *need* to hold the mutex, in general,
 * but they do in the basic wake-up flag scenario.  If not, the race is this:
 *
 * Sleeper:								Waker:
 * -----------------------------------------------------------------
 * Hold mutex
 *   See flag is False
 *   Decide to sleep
 *										Set flag True
 * PAUSE!								Grab CV lock
 *										See list is empty, unlock
 *
 *   Grab CV lock
 *     Get put on list
 *   Unlock CV lock
 * Unlock mutex
 * (Never wake up; we missed the signal)
 *
 * For those familiar with the kernel's CVs, we don't couple mutexes with CVs.
 * cv_lock() actually grabs the spinlock inside the CV and uses *that* to
 * protect the invariant.  The signallers always grab that lock, so the sleeper
 * is not in danger of missing the signal.  The tradeoff is that the kernel CVs
 * use a spinlock instead of a mutex for protecting its invariant; there might
 * be some case that preferred blocking sync.
 *
 * The uthread CVs take a mutex, unlike the kernel CVs, to map more cleanly to
 * POSIX CVs.  Maybe one approach or the other is a bad idea; we'll see.
 *
 * As far as lock ordering goes, once the sleeper holds the mutex and is on the
 * CV's list, it can unlock in any order it wants.  However, unlocking a mutex
 * actually requires grabbing its spinlock.  So as to not have a lock ordering
 * between *spinlocks*, we let go of the CV's spinlock before unlocking the
 * mutex.  There is an ordering between the mutex and the CV spinlock (mutex->cv
 * spin), but there is no ordering between the mutex spin and cv spin.  And of
 * course, we need to unlock the CV spinlock in the yield callback.
 *
 * Also note that we use the external API for the mutex operations.  A 2LS could
 * have their own mutex ops but still use the generic cv ops. */
static void uth_default_cv_wait(struct uth_default_cv *cv,
                                struct uth_default_mtx *mtx)
{
	struct uth_cv_link link;

	link.cv = cv;
	link.mtx = mtx;
	link.uth = current_uthread;
	spin_pdr_lock(&cv->lock);
	TAILQ_INSERT_TAIL(&cv->waiters, &link, next);
	uthread_yield(TRUE, __cv_wait_cb, &link);
	uth_mutex_lock((uth_mutex_t)mtx);
}

static void uth_default_cv_signal(struct uth_default_cv *cv)
{
	struct uth_cv_link *first;

	spin_pdr_lock(&cv->lock);
	first = TAILQ_FIRST(&cv->waiters);
	if (first)
		TAILQ_REMOVE(&cv->waiters, first, next);
	spin_pdr_unlock(&cv->lock);
	if (first)
		uthread_runnable(first->uth);
}

static void uth_default_cv_broadcast(struct uth_default_cv *cv)
{
	struct cv_link_tq restartees = TAILQ_HEAD_INITIALIZER(restartees);
	struct uth_cv_link *i, *safe;

	spin_pdr_lock(&cv->lock);
	TAILQ_SWAP(&cv->waiters, &restartees, uth_cv_link, next);
	spin_pdr_unlock(&cv->lock);
	/* Need the SAFE, since we can't touch the linkage once the uth could run */
	TAILQ_FOREACH_SAFE(i, &restartees, next, safe)
		uthread_runnable(i->uth);
}


/************** Wrappers for the uthread CV interface **************/


uth_cond_var_t uth_cond_var_alloc(void)
{
	if (sched_ops->cond_var_alloc)
		return sched_ops->cond_var_alloc();
	return (uth_cond_var_t)uth_default_cv_alloc();
}

void uth_cond_var_free(uth_cond_var_t cv)
{
	if (sched_ops->cond_var_free) {
		sched_ops->cond_var_free(cv);
		return;
	}
	uth_default_cv_free((struct uth_default_cv*)cv);
}

void uth_cond_var_wait(uth_cond_var_t cv, uth_mutex_t m)
{
	if (sched_ops->cond_var_wait) {
		sched_ops->cond_var_wait(cv, m);
		return;
	}
	uth_default_cv_wait((struct uth_default_cv*)cv, (struct uth_default_mtx*)m);
}

void uth_cond_var_signal(uth_cond_var_t cv)
{
	if (sched_ops->cond_var_signal) {
		sched_ops->cond_var_signal(cv);
		return;
	}
	uth_default_cv_signal((struct uth_default_cv*)cv);
}

void uth_cond_var_broadcast(uth_cond_var_t cv)
{
	if (sched_ops->cond_var_broadcast) {
		sched_ops->cond_var_broadcast(cv);
		return;
	}
	uth_default_cv_broadcast((struct uth_default_cv*)cv);
}
