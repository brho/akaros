/* Copyright (c) 2016-2017 Google, Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details. */

/* Generic Uthread Mutexes and CVs.  2LSs implement their own methods, but we
 * need a 2LS-independent interface and default implementation. */

#include <parlib/uthread.h>
#include <sys/queue.h>
#include <parlib/spinlock.h>
#include <malloc.h>


/************** Mutexes **************/


uth_mutex_t uth_mutex_alloc(void)
{
	struct uth_mutex *mtx;

	mtx = malloc(sizeof(struct uth_mutex));
	assert(mtx);
	spin_pdr_init(&mtx->lock);
	mtx->sync_obj = __uth_sync_alloc();
	mtx->locked = FALSE;
	return mtx;
}

void uth_mutex_free(uth_mutex_t mtx)
{
	__uth_sync_free(mtx->sync_obj);
	free(mtx);
}

static void __mutex_cb(struct uthread *uth, void *arg)
{
	struct uth_mutex *mtx = (struct uth_mutex*)arg;

	/* We need to tell the 2LS that its thread blocked.  We need to do this
	 * before unlocking the mtx, since as soon as we unlock, the mtx could be
	 * released and our thread restarted.
	 *
	 * Also note the lock-ordering rule.  The mtx lock is grabbed before any
	 * locks the 2LS might grab. */
	uthread_has_blocked(uth, mtx->sync_obj, UTH_EXT_BLK_MUTEX);
	spin_pdr_unlock(&mtx->lock);
}

void uth_mutex_lock(uth_mutex_t mtx)
{
	spin_pdr_lock(&mtx->lock);
	if (!mtx->locked) {
		mtx->locked = TRUE;
		spin_pdr_unlock(&mtx->lock);
		return;
	}
	/* the unlock and sync enqueuing is done in the yield callback.  as always,
	 * we need to do this part in vcore context, since as soon as we unlock the
	 * uthread could restart.  (atomically yield and unlock). */
	uthread_yield(TRUE, __mutex_cb, mtx);
}

bool uth_mutex_trylock(uth_mutex_t mtx)
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

void uth_mutex_unlock(uth_mutex_t mtx)
{
	struct uthread *uth;

	spin_pdr_lock(&mtx->lock);
	uth = __uth_sync_get_next(mtx->sync_obj);
	if (!uth)
		mtx->locked = FALSE;
	spin_pdr_unlock(&mtx->lock);
	if (uth)
		uthread_runnable(uth);
}


/************** Recursive mutexes **************/


uth_recurse_mutex_t uth_recurse_mutex_alloc(void)
{
	struct uth_recurse_mutex *r_mtx = malloc(sizeof(struct uth_recurse_mutex));

	assert(r_mtx);
	r_mtx->mtx = uth_mutex_alloc();
	r_mtx->lockholder = NULL;
	r_mtx->count = 0;
	return r_mtx;
}

void uth_recurse_mutex_free(uth_recurse_mutex_t r_mtx)
{
	uth_mutex_free(r_mtx->mtx);
	free(r_mtx);
}

void uth_recurse_mutex_lock(uth_recurse_mutex_t r_mtx)
{
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

bool uth_recurse_mutex_trylock(uth_recurse_mutex_t r_mtx)
{
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

void uth_recurse_mutex_unlock(uth_recurse_mutex_t r_mtx)
{
	r_mtx->count--;
	if (!r_mtx->count) {
		r_mtx->lockholder = NULL;
		uth_mutex_unlock(r_mtx->mtx);
	}
}


/************** Condition Variables **************/


uth_cond_var_t uth_cond_var_alloc(void)
{
	struct uth_cond_var *cv;

	cv = malloc(sizeof(struct uth_cond_var));
	assert(cv);
	spin_pdr_init(&cv->lock);
	cv->sync_obj = __uth_sync_alloc();
	return cv;
}

void uth_cond_var_free(uth_cond_var_t cv)
{
	__uth_sync_free(cv->sync_obj);
	free(cv);
}

struct uth_cv_link {
	struct uth_cond_var			*cv;
	struct uth_mutex			*mtx;
};

static void __cv_wait_cb(struct uthread *uth, void *arg)
{
	struct uth_cv_link *link = (struct uth_cv_link*)arg;
	struct uth_cond_var *cv = link->cv;
	struct uth_mutex *mtx = link->mtx;

	/* We need to tell the 2LS that its thread blocked.  We need to do this
	 * before unlocking the cv, since as soon as we unlock, the cv could be
	 * signalled and our thread restarted.
	 *
	 * Also note the lock-ordering rule.  The cv lock is grabbed before any
	 * locks the 2LS might grab. */
	uthread_has_blocked(uth, cv->sync_obj, UTH_EXT_BLK_MUTEX);
	spin_pdr_unlock(&cv->lock);
	/* This looks dangerous, since both the CV and MTX could use the
	 * uth->sync_next TAILQ_ENTRY (or whatever the 2LS uses), but the uthread
	 * never sleeps on both at the same time.  We *hold* the mtx - we aren't
	 * *sleeping* on it.  Sleeping uses the sync_next.  Holding it doesn't.
	 *
	 * Next, consider what happens as soon as we unlock the CV.  Our thread
	 * could get woken up, and then immediately try to grab the mtx and go to
	 * sleep! (see below).  If that happens, the uthread is no longer sleeping
	 * on the CV, and the sync_next is free.  The invariant is that a uthread
	 * can only sleep on one sync_object at a time. */
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
void uth_cond_var_wait(uth_cond_var_t cv, uth_mutex_t mtx)
{
	struct uth_cv_link link;

	link.cv = cv;
	link.mtx = mtx;
	spin_pdr_lock(&cv->lock);
	uthread_yield(TRUE, __cv_wait_cb, &link);
	uth_mutex_lock((uth_mutex_t)mtx);
}

void uth_cond_var_signal(uth_cond_var_t cv)
{
	struct uthread *uth;

	spin_pdr_lock(&cv->lock);
	uth = __uth_sync_get_next(cv->sync_obj);
	spin_pdr_unlock(&cv->lock);
	if (uth)
		uthread_runnable(uth);
}

void uth_cond_var_broadcast(uth_cond_var_t cv)
{
	struct uth_tailq restartees = TAILQ_HEAD_INITIALIZER(restartees);
	struct uthread *i, *safe;

	spin_pdr_lock(&cv->lock);
	/* If this turns out to be slow or painful for 2LSs, we can implement a
	 * get_all or something (default used to use TAILQ_SWAP). */
	while ((i = __uth_sync_get_next(cv->sync_obj))) {
		/* Once the uth is out of the sync obj, we can reuse sync_next. */
		TAILQ_INSERT_TAIL(&restartees, i, sync_next);
	}
	spin_pdr_unlock(&cv->lock);
	/* Need the SAFE, since we can't touch the linkage once the uth could run */
	TAILQ_FOREACH_SAFE(i, &restartees, sync_next, safe)
		uthread_runnable(i);
}


/************** Default Sync Obj Implementation **************/

static uth_sync_t uth_default_sync_alloc(void)
{
	struct uth_tailq *tq;

	tq = malloc(sizeof(struct uth_tailq));
	assert(tq);
	TAILQ_INIT(tq);
	return (uth_sync_t)tq;
}

static void uth_default_sync_free(uth_sync_t sync)
{
	struct uth_tailq *tq = (struct uth_tailq*)sync;

	assert(TAILQ_EMPTY(tq));
	free(tq);
}

static struct uthread *uth_default_sync_get_next(uth_sync_t sync)
{
	struct uth_tailq *tq = (struct uth_tailq*)sync;
	struct uthread *first;

	first = TAILQ_FIRST(tq);
	if (first)
		TAILQ_REMOVE(tq, first, sync_next);
	return first;
}

static bool uth_default_sync_get_uth(uth_sync_t sync, struct uthread *uth)
{
	struct uth_tailq *tq = (struct uth_tailq*)sync;
	struct uthread *i;

	TAILQ_FOREACH(i, tq, sync_next) {
		if (i == uth) {
			TAILQ_REMOVE(tq, i, sync_next);
			return TRUE;
		}
	}
	return FALSE;
}

/************** External uthread sync interface **************/

/* Called by the 2LS->has_blocked op, if they are using the default sync.*/
void __uth_default_sync_enqueue(struct uthread *uth, uth_sync_t sync)
{
	struct uth_tailq *tq = (struct uth_tailq*)sync;

	TAILQ_INSERT_TAIL(tq, uth, sync_next);
}

/* Called by 2LS-independent sync code when a sync object is created. */
uth_sync_t __uth_sync_alloc(void)
{
	if (sched_ops->sync_alloc)
		return sched_ops->sync_alloc();
	return uth_default_sync_alloc();
}

/* Called by 2LS-independent sync code when a sync object is destroyed. */
void __uth_sync_free(uth_sync_t sync)
{
	if (sched_ops->sync_free) {
		sched_ops->sync_free(sync);
		return;
	}
	uth_default_sync_free(sync);
}

/* Called by 2LS-independent sync code when a thread needs to be woken. */
struct uthread *__uth_sync_get_next(uth_sync_t sync)
{
	if (sched_ops->sync_get_next)
		return sched_ops->sync_get_next(sync);
	return uth_default_sync_get_next(sync);
}

/* Called by 2LS-independent sync code when a specific thread needs to be woken.
 * Returns TRUE if the uthread was blocked on the object, FALSE o/w. */
bool __uth_sync_get_uth(uth_sync_t sync, struct uthread *uth)
{
	if (sched_ops->sync_get_uth)
		return sched_ops->sync_get_uth(sync, uth);
	return uth_default_sync_get_uth(sync, uth);
}
