#pragma once

/*
 * Header file with infrastructure needed for userspace unit tests:
 *  - Assertion functions.
 *  - Test structures.
 *  - Launcher functions for test suites.
 */

#include <malloc.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <parlib/timing.h>
#include <execinfo.h>

__BEGIN_DECLS

/* 
 * Macros for assertions. 
 */
#define UT_ASSERT(test, ...)                                                 \
	UT_ASSERT_M(#test, test, __VA_ARGS__)

#define UT_ASSERT_M(message, test, ...)                                        \
do {                                                                           \
	if (!(test)) {                                                         \
		char fmt[] = "Assertion failure in %s() at %s:%d: %s";         \
		sprintf(utest_msg, fmt, __FUNCTION__, __FILE__, __LINE__,      \
			message);                                              \
		__VA_ARGS__;                                                   \
		return false;                                                  \
	}                                                                      \
} while (0)


/* If 'test' fails, Sets an assert message, which can be a format string, and
 * returns false. */
#define UT_ASSERT_FMT(message, test, ...)                                      \
do {                                                                           \
	if (!(test)) {                                                         \
		char fmt[] = "Assertion failure in %s() at %s:%d: " #message;  \
		sprintf(utest_msg, fmt, __func__, __FILE__, __LINE__,          \
		        ##__VA_ARGS__);                                        \
		return false;                                                  \
	}                                                                      \
} while (0)

/*
 * Structs and macros for registering test cases.
 */
struct utest {
	char name[256]; // Name of the test function.
	bool (*func)(void); // Name of the test function, should be = to 'name'.
	bool enabled; // Whether or not to run the test.
};

/* Used for defining an userspace test structure entry inline. */
#define UTEST_REG(name) \
	{"test_" #name, test_##name, true}

/*
 * Creates all the runnable code for a test suite.
 */
#define TEST_SUITE(__suite_name)                                               \
	char utest_msg[1024];                                                  \
	char suite_name[] = __suite_name;

#define RUN_TEST_SUITE(utests, num_utests, whitelist, whitelist_len)           \
do {                                                                           \
	if (whitelist_len > 0)                                                 \
		apply_whitelist(whitelist, whitelist_len, utests, num_utests); \
	run_utests(suite_name, utests, num_utests);                            \
} while (0)

/* Disables all the tests not passed through a whitelist. */
static void apply_whitelist(char *whitelist[], int whitelist_len,
			    struct utest tests[], int num_tests)
{
	for (int i = 0; i < num_tests; i++) {
		struct utest *test = &tests[i];

		if (test->enabled) {
			for (int j = 0; j < whitelist_len; ++j) {
				test->enabled = false;
				if (strcmp(test->name, whitelist[j]) == 0) {
					test->enabled = true;
					break;
				}
			}
		}
	}
}

static void run_utests(char *suite_name, struct utest tests[], int num_tests)
{
	extern char utest_msg[];
	printf("<-- BEGIN_USERSPACE_%s_TESTS -->\n", suite_name);

	for (int i=0; i<num_tests; i++) {
		struct utest *test = &tests[i];
		if (test->enabled) {
			uint64_t start = read_tsc();
			bool result = test->func();
			uint64_t end = read_tsc();
			uint64_t et_us = tsc2usec(end - start) % 1000000;
			uint64_t et_s = tsc2sec(end - start);

			char fmt[] = "\t%s   [%s](%llu.%06llus)   %s\n";
			if (result) {
				printf(fmt, "PASSED", test->name, et_s, et_us,
				       "");
			} else {
				printf(fmt, "FAILED", test->name, et_s, et_us,
				       utest_msg);
			}
		} else {
			printf("\tDISABLED [%s]\n", test->name);
		}
	}

	printf("<-- END_USERSPACE_%s_TESTS -->\n", suite_name);
}

__END_DECLS
