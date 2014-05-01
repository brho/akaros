#ifndef TESTS_TESTING_H
#define TESTS_TESTING_H

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
#include <timing.h>

/* 
 * Macros for assertions. 
 * UT_ASSERT_M prints a message in case of failure, UT_ASSERT just asserts.
 */
#define UT_ASSERT_M(message, test)                                               \
	do {                                                                         \
		if (!(test)) {                                                           \
			char prefix[] = "Assertion failed: ";                                \
			int msg_size = sizeof(prefix) + sizeof(message) - 1;                 \
			test_msg = (char*) malloc(msg_size);                                 \
			snprintf(test_msg, msg_size, "%s%s", prefix, message);               \
			return false;                                                        \
		}                                                                        \
	} while (0)

#define UT_ASSERT(test)                                                          \
	do {                                                                         \
		if (!(test)) {                                                           \
			return false;                                                        \
		}                                                                        \
	} while (0)



/*
 * Structs and macros for registering test cases.
 */
struct usertest {
	char name[256]; // Name of the test function.
	bool (*func)(void); // Name of the test function, should be equal to 'name'.
	bool enabled; // Whether or not to run the test.
};

/* Used for defining an userspace test structure entry inline. */
#define U_TEST_REG(name) \
	{"test_" #name, test_##name, true}



/*
 * Creates all the runnable code for a test suite.
 */
#define TEST_SUITE(__suite_name)                                                 \
	char *test_msg;                                                              \
	char suite_name[] = __suite_name;

#define RUN_TEST_SUITE(whitelist, whitelist_len)                                 \
	int num_tests = sizeof(usertests) / sizeof(struct usertest);                 \
	testing_main(whitelist, whitelist_len, usertests, num_tests);

/* Disables all the tests not passed through a whitelist. */
static void apply_whitelist(char *whitelist[], int whitelist_len,
	                        struct usertest tests[], int num_tests) {
	for (int i=0; i<num_tests; i++) {
		struct usertest *test = &tests[i];
		if (test->enabled) {
			for (int j = 1; j < whitelist_len; ++j) {
				test->enabled = false;
				if (strcmp(test->name, whitelist[j]) == 0) {
					test->enabled = true;
					break;
				}
			}
		}
	}
}



static int testing_main(char *whitelist[], int whitelist_len, 
	                    struct usertest tests[], int num_tests) {
	extern char *test_msg, suite_name[];
	printf("<-- BEGIN_USERSPACE_%s_TESTS -->\n", suite_name);

	// If any arguments are passed, treat them as a whitelist of what tests
	// to run (i.e., disable all the rest).
	if (whitelist_len > 0) {
		apply_whitelist(whitelist, whitelist_len, tests, num_tests);
	}

	// Run tests.
	for (int i=0; i<num_tests; i++) {
		struct usertest *test = &tests[i];
		if (test->enabled) {
			uint64_t start = read_tsc();
			bool result = test->func();
			uint64_t end = read_tsc();
			uint64_t et_us = tsc2usec(end - start) % 1000000;
			uint64_t et_s = tsc2sec(end - start);

			if (result) {
				printf("\tPASSED   [%s](%llu.%06llus)\n", test->name, et_s, 
				       et_us);
			} else {
				printf("\tFAILED   [%s](%llu.%06llus)  %s\n", test->name, et_s, 
				       et_us, test_msg);
				free(test_msg);
			}
		} else {
			printf("\tDISABLED [%s]\n", test->name);
		}
	}

	printf("<-- END_USERSPACE_%s_TESTS -->\n", suite_name);
}

#endif // TESTS_TESTING_H
