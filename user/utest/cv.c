/* Copyright (c) 2016 Google, Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details. */

#include <utest/utest.h>
#include <parlib/uthread.h>
#include <pthread.h>

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

/* <--- End definition of test cases ---> */

struct utest utests[] = {
	UTEST_REG(signal_no_wait),
	UTEST_REG(signal),
	UTEST_REG(broadcast),
	UTEST_REG(recurse),
	UTEST_REG(recurse_static),
	UTEST_REG(semaphore),
	UTEST_REG(semaphore_static),
};
int num_utests = sizeof(utests) / sizeof(struct utest);

int main(int argc, char *argv[])
{
	// Run test suite passing it all the args as whitelist of what tests to run.
	char **whitelist = &argv[1];
	int whitelist_len = argc - 1;

	RUN_TEST_SUITE(utests, num_utests, whitelist, whitelist_len);
}
