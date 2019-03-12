/* Copyright (c) 2016 Google, Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details. */

#include <utest/utest.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

/* This tests a bug where if a thread other than thread0 registered an atexit()
 * function, we'd GPF when thread0 exited.  Due to the way our infrastructure
 * prints tests results before the program exited, you may see this say that the
 * test succeeded, but then fail later with a GPF or other assertion. */

TEST_SUITE("AT-EXIT");

/* <--- Begin definition of test cases ---> */

static bool child_ran_atexit = FALSE;

static void child_atexit(void)
{
	child_ran_atexit = TRUE;
}

static void *child_func(void *arg)
{
	atexit(child_atexit);
	return 0;
}

static void main_atexit(void)
{
	/* Using the non-UT assert, since the test_atexit_threads() has already
	 * returned.  Also, since the child called atexit() after main, its
	 * handler should run first, according to the man page. */
	assert(child_ran_atexit);
}

bool test_atexit_threads(void)
{
	pthread_t child;
	void *child_ret;

	atexit(main_atexit);
	pthread_create(&child, NULL, &child_func, NULL);
	pthread_join(child, &child_ret);
	return TRUE;
}

/* <--- End definition of test cases ---> */

struct utest utests[] = {
	UTEST_REG(atexit_threads),
};
int num_utests = sizeof(utests) / sizeof(struct utest);

int main(int argc, char *argv[])
{
	char **whitelist = &argv[1];
	int whitelist_len = argc - 1;

	RUN_TEST_SUITE(utests, num_utests, whitelist, whitelist_len);
}
