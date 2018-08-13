/* Copyright (c) 2016 Google, Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details. */

#include <utest/utest.h>
#include <parlib/uthread.h>
#include <pthread.h>
#include <time.h>

TEST_SUITE("CV");

/* <--- Begin definition of test cases ---> */

struct common_args {
	uth_cond_var_t				*cv;
	uth_mutex_t					*mtx;
	bool						flag;
	unsigned int				wait_sleep;
	unsigned int				sig_sleep;
};

#define PTH_TEST_TRUE			(void*)1

bool test_signal_no_wait(void)
{
	uth_cond_var_t *cv = uth_cond_var_alloc();

	uth_cond_var_broadcast(cv);
	uth_cond_var_signal(cv);
	pthread_yield();
	uth_cond_var_free(cv);
	return TRUE;
}

void *__cv_signaller(void *arg)
{
	struct common_args *args = (struct common_args*)arg;

	uthread_usleep(args->sig_sleep);
	uth_mutex_lock(args->mtx);
	args->flag = TRUE;
	uth_mutex_unlock(args->mtx);
	/* We can actually signal outside the mutex if we want.  Remember the
	 * invariant: whenever the flag is set from FALSE to TRUE, all waiters that
	 * saw FALSE are on the CV's waitqueue.  That's true even after we unlock
	 * the mutex. */
	uth_cond_var_signal(args->cv);
	return PTH_TEST_TRUE;
}

void *__cv_broadcaster(void *arg)
{
	struct common_args *args = (struct common_args*)arg;

	uthread_usleep(args->sig_sleep);
	uth_mutex_lock(args->mtx);
	args->flag = TRUE;
	uth_mutex_unlock(args->mtx);
	/* We can actually signal outside the mutex if we want.  Remember the
	 * invariant: whenever the flag is set from FALSE to TRUE, all waiters that
	 * saw FALSE are on the CV's waitqueue.  That's true even after we unlock
	 * the mutex. */
	uth_cond_var_broadcast(args->cv);
	return PTH_TEST_TRUE;
}

void *__cv_waiter(void *arg)
{
	struct common_args *args = (struct common_args*)arg;

	uthread_usleep(args->wait_sleep);
	uth_mutex_lock(args->mtx);
	while (!args->flag)
		uth_cond_var_wait(args->cv, args->mtx);
	UT_ASSERT(args->flag);
	uth_mutex_unlock(args->mtx);
	return PTH_TEST_TRUE;
}

/* Basic one signaller, one receiver.  We want to vary the amount of time the
 * sender and receiver delays, starting with (1ms, 0ms) and ending with (0ms,
 * 1ms).  At each extreme, such as with the sender waiting 1ms, the
 * receiver/waiter should hit the "check and wait" point well before the
 * sender/signaller hits the "change state and signal" point. */
bool test_signal(void)
{
	struct common_args local_a, *args = &local_a;
	pthread_t signaller, waiter;
	void *sig_join, *wait_join;
	int ret;
	static uth_mutex_t static_mtx = UTH_MUTEX_INIT;
	static uth_cond_var_t static_cv = UTH_COND_VAR_INIT;

	/* Also testing the static initializers.  Note we never free these. */
	args->cv = &static_cv;
	args->mtx = &static_mtx;

	for (int i = 0; i < 1000; i += 10) {
		args->flag = FALSE;
		args->wait_sleep = i;
		args->sig_sleep = 1000 - i;

		ret = pthread_create(&waiter, 0, __cv_waiter, args);
		UT_ASSERT(!ret);
		ret = pthread_create(&signaller, 0, __cv_signaller, args);
		UT_ASSERT(!ret);
		ret = pthread_join(waiter, &wait_join);
		UT_ASSERT(!ret);
		ret = pthread_join(signaller, &sig_join);
		UT_ASSERT(!ret);
		UT_ASSERT_M("Waiter Failed", wait_join == PTH_TEST_TRUE);
		UT_ASSERT_M("Signaller Failed", sig_join == PTH_TEST_TRUE);
	}

	return TRUE;
}

bool test_broadcast(void)
{
	#define NR_WAITERS 20
	struct common_args local_a, *args = &local_a;
	pthread_t bcaster, waiters[NR_WAITERS];
	void *bcast_join, *wait_joins[NR_WAITERS];
	int ret;

	args->cv = uth_cond_var_alloc();
	args->mtx = uth_mutex_alloc();
	args->flag = FALSE;
	args->wait_sleep = 0;
	args->sig_sleep = 1000;

	for (int i = 0; i < NR_WAITERS; i++) {
		ret = pthread_create(&waiters[i], 0, __cv_waiter, args);
		UT_ASSERT(!ret);
	}
	ret = pthread_create(&bcaster, 0, __cv_broadcaster, args);
	UT_ASSERT(!ret);

	ret = pthread_join(bcaster, &bcast_join);
	UT_ASSERT(!ret);
	UT_ASSERT_M("Broadcaster Failed", bcast_join == PTH_TEST_TRUE);
	for (int i = 0; i < NR_WAITERS; i++) {
		ret = pthread_join(waiters[i], &wait_joins[i]);
		UT_ASSERT(!ret);
		UT_ASSERT_M("Waiter Failed", wait_joins[i] == PTH_TEST_TRUE);
	}

	uth_cond_var_free(args->cv);
	uth_mutex_free(args->mtx);
	return TRUE;
}

static void *__cv_signaller_no_mutex(void *arg)
{
	struct common_args *args = (struct common_args*)arg;

	uthread_usleep(args->sig_sleep);
	uth_cond_var_lock(args->cv);
	args->flag = TRUE;
	__uth_cond_var_signal_and_unlock(args->cv);
	return PTH_TEST_TRUE;
}

static void *__cv_broadcaster_no_mutex(void *arg)
{
	struct common_args *args = (struct common_args*)arg;

	uthread_usleep(args->sig_sleep);
	uth_cond_var_lock(args->cv);
	args->flag = TRUE;
	__uth_cond_var_broadcast_and_unlock(args->cv);
	return PTH_TEST_TRUE;
}

static void *__cv_waiter_no_mutex(void *arg)
{
	struct common_args *args = (struct common_args*)arg;

	uthread_usleep(args->wait_sleep);
	uth_cond_var_lock(args->cv);
	while (!args->flag)
		uth_cond_var_wait(args->cv, NULL);
	UT_ASSERT(args->flag);
	uth_cond_var_unlock(args->cv);
	return PTH_TEST_TRUE;
}

bool test_signal_no_mutex(void)
{
	struct common_args local_a, *args = &local_a;
	pthread_t signaller, waiter;
	void *sig_join, *wait_join;
	int ret;
	static uth_cond_var_t static_cv = UTH_COND_VAR_INIT;

	args->cv = &static_cv;
	args->mtx = NULL;

	for (int i = 0; i < 1000; i += 10) {
		args->flag = FALSE;
		args->wait_sleep = i;
		args->sig_sleep = 1000 - i;

		ret = pthread_create(&waiter, 0, __cv_waiter_no_mutex, args);
		UT_ASSERT(!ret);
		ret = pthread_create(&signaller, 0, __cv_signaller_no_mutex, args);
		UT_ASSERT(!ret);
		ret = pthread_join(waiter, &wait_join);
		UT_ASSERT(!ret);
		ret = pthread_join(signaller, &sig_join);
		UT_ASSERT(!ret);
		UT_ASSERT_M("Waiter Failed", wait_join == PTH_TEST_TRUE);
		UT_ASSERT_M("Signaller Failed", sig_join == PTH_TEST_TRUE);
	}

	return TRUE;
}

bool test_broadcast_no_mutex(void)
{
	#define NR_WAITERS 20
	struct common_args local_a, *args = &local_a;
	pthread_t bcaster, waiters[NR_WAITERS];
	void *bcast_join, *wait_joins[NR_WAITERS];
	int ret;

	args->cv = uth_cond_var_alloc();
	args->mtx = NULL;
	args->flag = FALSE;
	args->wait_sleep = 0;
	args->sig_sleep = 1000;

	for (int i = 0; i < NR_WAITERS; i++) {
		ret = pthread_create(&waiters[i], 0, __cv_waiter_no_mutex, args);
		UT_ASSERT(!ret);
	}
	ret = pthread_create(&bcaster, 0, __cv_broadcaster_no_mutex, args);
	UT_ASSERT(!ret);

	ret = pthread_join(bcaster, &bcast_join);
	UT_ASSERT(!ret);
	UT_ASSERT_M("Broadcaster Failed", bcast_join == PTH_TEST_TRUE);
	for (int i = 0; i < NR_WAITERS; i++) {
		ret = pthread_join(waiters[i], &wait_joins[i]);
		UT_ASSERT(!ret);
		UT_ASSERT_M("Waiter Failed", wait_joins[i] == PTH_TEST_TRUE);
	}

	uth_cond_var_free(args->cv);
	return TRUE;
}

static bool __test_recurse(struct uth_recurse_mutex *r_mtx)
{
	bool test;

	uth_recurse_mutex_lock(r_mtx);
	/* should be one lock deep */
	UT_ASSERT(r_mtx->count == 1);
	UT_ASSERT(r_mtx->lockholder == current_uthread);

	test = uth_recurse_mutex_trylock(r_mtx);
	UT_ASSERT_M("Failed to re(try)lock our owned mutex!", test);
	/* should be two locks deep */
	UT_ASSERT(r_mtx->count == 2);
	UT_ASSERT(r_mtx->lockholder == current_uthread);

	uth_recurse_mutex_lock(r_mtx);
	/* should be three locks deep */
	UT_ASSERT(r_mtx->count == 3);
	UT_ASSERT(r_mtx->lockholder == current_uthread);

	uth_recurse_mutex_unlock(r_mtx);
	/* should be two locks deep */
	UT_ASSERT(r_mtx->count == 2);
	UT_ASSERT(r_mtx->lockholder == current_uthread);

	uth_recurse_mutex_unlock(r_mtx);
	/* should be one lock deep */
	UT_ASSERT(r_mtx->count == 1);
	UT_ASSERT(r_mtx->lockholder == current_uthread);

	uth_recurse_mutex_unlock(r_mtx);
	/* should be unlocked */
	UT_ASSERT(r_mtx->count == 0);
	UT_ASSERT(!r_mtx->lockholder);

	return TRUE;
}

bool test_recurse(void)
{
	uth_recurse_mutex_t *r_mtx;

	r_mtx = uth_recurse_mutex_alloc();
	UT_ASSERT(r_mtx);
	if (!__test_recurse(r_mtx))
		return FALSE;
	uth_recurse_mutex_free(r_mtx);

	return TRUE;
}

bool test_recurse_static(void)
{
	static uth_recurse_mutex_t static_recurse_mtx = UTH_RECURSE_MUTEX_INIT;

	return __test_recurse(&static_recurse_mtx);
}

static bool __test_semaphore(struct uth_semaphore *sem, int count)
{
	bool can_down;

	UT_ASSERT(count > 2);	/* for our own use */
	UT_ASSERT(sem->count == count);
	/* should be able to down it count times without blocking */
	for (int i = 0; i < count; i++) {
		uth_semaphore_down(sem);
		UT_ASSERT(sem->count == count - (i + 1));
	}

	/* shouldn't be able to down, since we grabbed them all */
	can_down = uth_semaphore_trydown(sem);
	UT_ASSERT(!can_down);
	UT_ASSERT(sem->count == 0);

	uth_semaphore_up(sem);
	UT_ASSERT(sem->count == 1);
	can_down = uth_semaphore_trydown(sem);
	UT_ASSERT(can_down);
	UT_ASSERT(sem->count == 0);

	for (int i = 0; i < count; i++) {
		uth_semaphore_up(sem);
		UT_ASSERT(sem->count == i + 1);
	}

	return TRUE;
}

bool test_semaphore(void)
{
	uth_semaphore_t *sem;

	sem = uth_semaphore_alloc(5);
	UT_ASSERT(sem);
	if (!__test_semaphore(sem, 5))
		return FALSE;
	uth_semaphore_free(sem);

	return TRUE;
}

bool test_semaphore_static(void)
{
	static uth_semaphore_t static_semaphore = UTH_SEMAPHORE_INIT(5);

	return __test_semaphore(&static_semaphore, 5);
}

bool test_semaphore_timeout(void)
{
	static uth_semaphore_t sem = UTH_SEMAPHORE_INIT(1);
	struct timespec timeout[1];
	int ret;
	bool got_it;

	ret = clock_gettime(CLOCK_REALTIME, timeout);
	UT_ASSERT(!ret);
	timeout->tv_nsec += 500000000;
	got_it = uth_semaphore_timed_down(&sem, timeout);
	UT_ASSERT(got_it);

	/* Second time we still hold the sem and would block and should time out. */
	UT_ASSERT(sem.count == 0);
	ret = clock_gettime(CLOCK_REALTIME, timeout);
	UT_ASSERT(!ret);
	timeout->tv_nsec += 500000000;
	got_it = uth_semaphore_timed_down(&sem, timeout);
	UT_ASSERT(!got_it);

	return TRUE;
}

bool test_cv_timeout(void)
{
	static uth_mutex_t mtx = UTH_MUTEX_INIT;
	static uth_cond_var_t cv = UTH_COND_VAR_INIT;
	struct timespec timeout[1];
	int ret;
	bool was_signalled;

	uth_mutex_lock(&mtx);
	ret = clock_gettime(CLOCK_REALTIME, timeout);
	UT_ASSERT(!ret);
	timeout->tv_nsec += 500000000;
	was_signalled = uth_cond_var_timed_wait(&cv, &mtx, timeout);
	UT_ASSERT(!was_signalled);
	UT_ASSERT(mtx.count == 0);	/* semaphore's count variable */
	uth_mutex_unlock(&mtx);
	UT_ASSERT(mtx.count == 1);

	return TRUE;
}

bool test_cv_recurse_timeout(void)
{
	static uth_recurse_mutex_t r_mtx = UTH_RECURSE_MUTEX_INIT;
	static uth_cond_var_t cv = UTH_COND_VAR_INIT;
	struct timespec timeout[1];
	int ret;
	bool was_signalled;

	/* Get three-deep locks, make sure the bookkeeping is right */
	uth_recurse_mutex_lock(&r_mtx);
	uth_recurse_mutex_lock(&r_mtx);
	uth_recurse_mutex_lock(&r_mtx);
	UT_ASSERT(r_mtx.count == 3);
	UT_ASSERT(r_mtx.mtx.count == 0);

	ret = clock_gettime(CLOCK_REALTIME, timeout);
	UT_ASSERT(!ret);
	timeout->tv_nsec += 500000000;
	was_signalled = uth_cond_var_timed_wait_recurse(&cv, &r_mtx, timeout);
	UT_ASSERT(!was_signalled);
	UT_ASSERT(r_mtx.count == 3);
	UT_ASSERT(r_mtx.mtx.count == 0);

	/* Unlock our three locks, then make sure the semaphore/mtx is unlocked. */
	uth_recurse_mutex_unlock(&r_mtx);
	uth_recurse_mutex_unlock(&r_mtx);
	uth_recurse_mutex_unlock(&r_mtx);
	UT_ASSERT(r_mtx.count == 0);
	UT_ASSERT(r_mtx.mtx.count == 1);

	return TRUE;
}

bool test_cv_timeout_no_mutex(void)
{
	static uth_cond_var_t cv = UTH_COND_VAR_INIT;
	struct timespec timeout[1];
	int ret;
	bool was_signalled;

	uth_cond_var_lock(&cv);
	ret = clock_gettime(CLOCK_REALTIME, timeout);
	UT_ASSERT(!ret);
	timeout->tv_nsec += 500000000;
	was_signalled = uth_cond_var_timed_wait(&cv, NULL, timeout);
	UT_ASSERT(!was_signalled);
	uth_cond_var_unlock(&cv);

	return TRUE;
}

static uth_rwlock_t rwl = UTH_RWLOCK_INIT;
static int rw_value;

static void *rw_reader(void *arg)
{
	int val;

	uth_rwlock_rdlock(&rwl);
	if (!rw_value) {
		/* We won, let's wait a while to let the writer block (racy) */
		uthread_usleep(5000);
	}
	uth_rwlock_unlock(&rwl);
	while (1) {
		uth_rwlock_rdlock(&rwl);
		val = rw_value;
		uth_rwlock_unlock(&rwl);
		if (val)
			break;
		uthread_usleep(1000);
	}
	return 0;
}

static void *rw_writer(void *arg)
{
	/* Let the readers get a head start */
	uthread_usleep(1000);
	/* This might fail, but can't hurt to try it */
	if (uth_rwlock_try_wrlock(&rwl))
		uth_rwlock_unlock(&rwl);
	/* Eventually we'll get the lock */
	uth_rwlock_wrlock(&rwl);
	rw_value = 1;
	uth_rwlock_unlock(&rwl);
	return 0;
}

bool test_rwlock(void)
{
	#define NR_THREADS 3
	struct uth_join_request joinees[NR_THREADS];
	void *retvals[NR_THREADS];

	joinees[0].uth = uthread_create(rw_reader, NULL);
	joinees[1].uth = uthread_create(rw_reader, NULL);
	joinees[2].uth = uthread_create(rw_writer, NULL);
	for (int i = 0; i < NR_THREADS; i++)
		joinees[i].retval_loc = &retvals[i];
	uthread_join_arr(joinees, NR_THREADS);
	for (int i = 0; i < NR_THREADS; i++)
		UT_ASSERT(retvals[i] == 0);
	return TRUE;
}

/* <--- End definition of test cases ---> */

struct utest utests[] = {
	UTEST_REG(signal_no_wait),
	UTEST_REG(signal),
	UTEST_REG(broadcast),
	UTEST_REG(signal_no_mutex),
	UTEST_REG(broadcast_no_mutex),
	UTEST_REG(recurse),
	UTEST_REG(recurse_static),
	UTEST_REG(semaphore),
	UTEST_REG(semaphore_static),
	UTEST_REG(semaphore_timeout),
	UTEST_REG(cv_timeout),
	UTEST_REG(cv_timeout_no_mutex),
	UTEST_REG(cv_recurse_timeout),
	UTEST_REG(rwlock),
};
int num_utests = sizeof(utests) / sizeof(struct utest);

int main(int argc, char *argv[])
{
	// Run test suite passing it all the args as whitelist of what tests to run.
	char **whitelist = &argv[1];
	int whitelist_len = argc - 1;

	RUN_TEST_SUITE(utests, num_utests, whitelist, whitelist_len);
}
