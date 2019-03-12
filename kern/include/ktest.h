#pragma once

/*
 * Header file with infrastructure needed for kernel unit tests:
 *  - Assertion functions.
 *  - Test structures.
 */

#include <common.h>
#include <stdio.h>
#include <stdbool.h>
#include <kmalloc.h>
#include <arch/arch.h>
#include <time.h>
#include <sys/queue.h>

/* Macros for assertions.
 */
#define KT_ASSERT(test)                                                        \
	KT_ASSERT_M("", test)

#define KT_ASSERT_M(message, test)                                             \
do {                                                                           \
	if (!(test)) {                                                         \
		char fmt[] = "Assertion failure in %s() at %s:%d: %s";         \
		snprintf(ktest_msg, sizeof(ktest_msg), fmt, __FUNCTION__,      \
		         __FILE__, __LINE__, message);                         \
		return false;                                                  \
	}                                                                      \
} while (0)

struct ktest {
	char name[256]; // Name of the test function.
	bool (*func)(void); // Name of the test function, should be = to 'name'.
	bool enabled; // Whether to run or not the test.
};

struct ktest_suite {
    SLIST_ENTRY(ktest_suite) link;
	char name[256];
	struct ktest *ktests;
	int num_ktests;
};

#define KTEST_SUITE(name) \
	static struct ktest_suite ktest_suite = {{}, name, NULL, 0};

#define KTEST_REG(name, config) \
	{"test_" #name, test_##name, is_defined(config)}

#define REGISTER_KTESTS(ktests, num_ktests)                                    \
do {                                                                           \
	ktest_suite.ktests = ktests;                                           \
	ktest_suite.num_ktests = num_ktests;                                   \
	register_ktest_suite(&ktest_suite);                                    \
} while (0)

/* Global string used to report info about the last completed test */
extern char ktest_msg[1024];

void register_ktest_suite(struct ktest_suite *suite);
void run_ktest_suite(struct ktest_suite *suite);
void run_registered_ktest_suites();
