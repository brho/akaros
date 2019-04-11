#include <parlib/common.h>
#include <futex.h>
#include <sys/queue.h>
#include <parlib/uthread.h>
#include <parlib/parlib.h>
#include <parlib/assert.h>
#include <parlib/stdio.h>
#include <errno.h>
#include <parlib/slab.h>
#include <parlib/mcs.h>
#include <parlib/alarm.h>

struct futex_element {
	TAILQ_ENTRY(futex_element) link;
	int *uaddr;
	bool on_list;
	bool waker_using;
	uth_cond_var_t cv;
};
TAILQ_HEAD(futex_queue, futex_element);

struct futex_data {
	struct mcs_pdr_lock lock;
	struct futex_queue queue;
};
static struct futex_data __futex;

static inline void futex_init(void *arg)
{
	mcs_pdr_init(&__futex.lock);
	TAILQ_INIT(&__futex.queue);
}

static inline int futex_wait(int *uaddr, int val,
                             const struct timespec *abs_timeout)
{
	struct futex_element e[1];
	bool timed_out;

	mcs_pdr_lock(&__futex.lock);
	if (*uaddr != val) {
		mcs_pdr_unlock(&__futex.lock);
		return 0;
	}
	e->uaddr = uaddr;
	uth_cond_var_init(&e->cv);
	e->waker_using = false;
	e->on_list = true;
	TAILQ_INSERT_TAIL(&__futex.queue, e, link);
	/* Lock switch.  Any waker will grab the global lock, then grab ours.
	 * We're downgrading to the CV lock, which still protects us from
	 * missing the signal (which is someone calling Wake after changing
	 * *uaddr).  The CV code will atomically block (with timeout) and unlock
	 * the CV lock.
	 *
	 * Ordering is __futex.lock -> CV lock, but you can have the inner lock
	 * without holding the outer lock. */
	uth_cond_var_lock(&e->cv);
	mcs_pdr_unlock(&__futex.lock);

	timed_out = !uth_cond_var_timed_wait(&e->cv, NULL, abs_timeout);
	/* CV wait returns with the lock held, which is unneccessary for
	 * futexes.  We could use this cv lock and maybe a trylock on the futex
	 * to sync with futex_wake, instead of the current synchronization
	 * techniques with the bools. */
	uth_cond_var_unlock(&e->cv);

	/* In the common case, the waker woke us and already cleared on_list,
	 * and we'd rather not grab the __futex lock again.  Note the outer
	 * on_list check is an optimization, and we need the lock to be sure.
	 * Also note the waker sets waker_using before on_list, so if we happen
	 * to see !on_list (while the waker is mucking with the list), we'll see
	 * waker_using and spin below. */
	if (e->on_list) {
		mcs_pdr_lock(&__futex.lock);
		if (e->on_list)
			TAILQ_REMOVE(&__futex.queue, e, link);
		mcs_pdr_unlock(&__futex.lock);
	}
	rmb();	/* read on_list before waker_using */
	/* The waker might have yanked us and is about to kick the CV.  Need to
	 * wait til they are done before freeing e. */
	while (e->waker_using)
		cpu_relax_any();

	if (timed_out) {
		errno = ETIMEDOUT;
		return -1;
	}
	return 0;
}

static inline int futex_wake(int *uaddr, int count)
{
	int max = count;
	struct futex_element *e, *temp;
	struct futex_queue q = TAILQ_HEAD_INITIALIZER(q);

	/* The waiter spins on us with cpu_relax_any().  That code assumes the
	 * target of the wait/spin is in vcore context, or at least has notifs
	 * disabled. */
	uth_disable_notifs();
	mcs_pdr_lock(&__futex.lock);
	TAILQ_FOREACH_SAFE(e, &__futex.queue, link, temp) {
		if (count <= 0)
			break;
		if (e->uaddr == uaddr) {
			e->waker_using = true;
			/* flag waker_using before saying !on_list */
			wmb();
			e->on_list = false;
			TAILQ_REMOVE(&__futex.queue, e, link);
			TAILQ_INSERT_TAIL(&q, e, link);
			count--;
		}
	}
	mcs_pdr_unlock(&__futex.lock);
	TAILQ_FOREACH_SAFE(e, &q, link, temp) {
		TAILQ_REMOVE(&q, e, link);
		uth_cond_var_signal(&e->cv);
		/* Do not touch e after marking it. */
		e->waker_using = false;
	}
	uth_enable_notifs();

	return max - count;
}

int futex(int *uaddr, int op, int val, const struct timespec *timeout,
          int *uaddr2, int val3)
{
	static parlib_once_t once = PARLIB_ONCE_INIT;
	struct timespec abs_timeout[1];

	parlib_run_once(&once, futex_init, NULL);
	assert(uaddr2 == NULL);
	assert(val3 == 0);

	/* futex timeouts are relative.  Internally, we use absolute timeouts */
	if (timeout) {
		clock_gettime(CLOCK_MONOTONIC, abs_timeout);
		/* timespec_add is available inside glibc, but not out here. */
		abs_timeout->tv_sec += timeout->tv_sec;
		abs_timeout->tv_nsec += timeout->tv_nsec;
		if (abs_timeout->tv_nsec >= 1000000000) {
			abs_timeout->tv_nsec -= 1000000000;
			abs_timeout->tv_sec++;
		}
	}

	switch (op) {
	case FUTEX_WAIT:
		return futex_wait(uaddr, val, timeout ? abs_timeout : NULL);
	case FUTEX_WAKE:
		return futex_wake(uaddr, val);
	default:
		errno = ENOSYS;
		return -1;
	}
	return -1;
}
