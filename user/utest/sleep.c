/* Copyright (c) 2015 Google Inc
 * Kevin Klues <klueska@google.com>
 * See LICENSE for details. */

#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <ros/time.h>
#include <utest/utest.h>

TEST_SUITE("SLEEP");

/* Offset in microseconds to account for processing overhead, etc.. */
uint64_t offset_usec = 500;
/* Number of times to run each test. Take the minimum of the results. */
const int NUM_ITERATIONS = 5;

bool test_nanosleep(void)
{
	const struct timespec times[] = {
		{0, 0},
		{0, 1},
		{0, 999},
		{0, 1000},
		{0, 1001},
		{1, 0},
		{1, 1},
		{1, 999},
		{1, 1000},
		{1, 1001},
		{2, 123456789},
	};

	#define check_ntimes(idx, lbound, ubound) \
	{ \
		uint64_t tsc, diff_usec = INT_MAX; \
		for (int i = 0; i < NUM_ITERATIONS; i++) { \
			tsc = read_tsc(); \
			nanosleep(&times[idx], NULL); \
			diff_usec = MIN(diff_usec, tsc2usec(read_tsc() - tsc)); \
		} \
		UT_ASSERT_M("Sleep finished too soon", diff_usec >= lbound); \
		UT_ASSERT_M("Sleep finished too late", diff_usec <= ubound); \
	}

	check_ntimes(0, 0, offset_usec + 0);
	check_ntimes(1, 0, offset_usec + 1);
	check_ntimes(2, 0, offset_usec + 1);
	check_ntimes(3, 0, offset_usec + 2);
	check_ntimes(4, 0, offset_usec + 2);
	check_ntimes(5, 1000000, offset_usec + 1000000);
	check_ntimes(6, 1000000, offset_usec + 1000001);
	check_ntimes(7, 1000000, offset_usec + 1000001);
	check_ntimes(8, 1000000, offset_usec + 1000002);
	check_ntimes(9, 1000000, offset_usec + 1000002);
	check_ntimes(10, 2000000, offset_usec + 2123456);

	/* TODO: Check abort path with 'remaining' parameter. */
	return true;
}

bool test_usleep(void)
{
	const useconds_t times[] = {
		0, 1, 1000, 2000, 1000000, 2000000
	};

	#define check_utimes(idx, lbound, ubound) \
	{ \
		uint64_t tsc, diff_usec = INT_MAX; \
		for (int i = 0; i < NUM_ITERATIONS; i++) { \
			tsc = read_tsc(); \
			usleep(times[idx]); \
			diff_usec = MIN(diff_usec, tsc2usec(read_tsc() - tsc)); \
		} \
		UT_ASSERT_M("Sleep finished too soon", diff_usec >= lbound); \
		UT_ASSERT_M("Sleep finished too late", diff_usec <= ubound); \
	}

	check_utimes(0, 0, offset_usec + 0);
	check_utimes(1, 1, offset_usec + 1);
	check_utimes(2, 1000, offset_usec + 1001);
	check_utimes(3, 2000, offset_usec + 20001);
	check_utimes(4, 1000000, offset_usec + 1000001);
	check_utimes(5, 2000000, offset_usec + 2000001);
	return true;
}

bool test_sleep(void)
{
	const unsigned int times[] = {
		0, 1, 2, 3, 4, 5
	};

	#define check_times(idx, lbound, ubound) \
	{ \
		uint64_t tsc, diff_sec = INT_MAX; \
		for (int i = 0; i < NUM_ITERATIONS; i++) { \
			tsc = read_tsc(); \
			sleep(times[idx]); \
			diff_sec = MIN(diff_sec, tsc2sec(read_tsc() - tsc)); \
		} \
		UT_ASSERT_M("Sleep finished too soon", diff_sec >= lbound); \
		UT_ASSERT_M("Sleep finished too late", diff_sec <= ubound); \
	}

	check_times(0, 0, 1);
	check_times(1, 1, 2);
	check_times(2, 2, 3);
	check_times(3, 3, 4);
	check_times(4, 4, 5);
	check_times(5, 5, 6);
	return true;
}

struct utest utests[] = {
	UTEST_REG(nanosleep),
	UTEST_REG(usleep),
	UTEST_REG(sleep),
};
int num_utests = sizeof(utests) / sizeof(struct utest);

int main(int argc, char *argv[])
{
	char **whitelist = &argv[1];
	int whitelist_len = argc - 1;

	RUN_TEST_SUITE(utests, num_utests, whitelist, whitelist_len);
}
