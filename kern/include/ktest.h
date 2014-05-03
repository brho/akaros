#ifndef ROS_INC_KTEST_H
#define ROS_INC_KTEST_H

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

/* Global string used to report info about the last completed test */
extern char ktest_msg[1024];

/* Macros for assertions. 
 */
#define KT_ASSERT(test)                                                          \
	KT_ASSERT_M("", test)

#define KT_ASSERT_M(message, test)                                               \
	do {                                                                         \
		if (!(test)) {                                                           \
			char fmt[] = "Assertion failure in %s() at %s:%d: %s";               \
			snprintf(ktest_msg, sizeof(ktest_msg), fmt, __FUNCTION__,            \
			         __FILE__, __LINE__, message);                               \
			return false;                                                        \
		}                                                                        \
	} while (0)

struct ktest {
	char name[256]; // Name of the test function.
	bool (*func)(void); // Name of the test function, should be equal to 'name'.
	bool enabled; // Whether to run or not the test.
};

/* Macro for registering a kernel test. */
#define KTEST_REG(name, config) \
	{"test_" #name, test_##name, is_defined(config)}

static void run_ktests(char *suite_name, struct ktest tests[], int num_tests)
{
	extern char ktest_msg[];
	printk("<-- BEGIN_KERNEL_%s_TESTS -->\n", suite_name);

	for (int i=0; i<num_tests; i++) {
		struct ktest *test = &tests[i];
		if (test->enabled) {
			uint64_t start = read_tsc();
			bool result = test->func();
			uint64_t end = read_tsc();
			uint64_t et_us = tsc2usec(end - start) % 1000000;
			uint64_t et_s = tsc2sec(end - start);

			char fmt[] = "\t%s   [%s](%llu.%06llus)   %s\n";
			if (result) {
				printk(fmt, "PASSED", test->name, et_s, et_us, "");
			} else {
				printk(fmt, "FAILED", test->name, et_s, et_us, ktest_msg);
			}
		} else {
			printk("\tDISABLED [%s]\n", test->name);
		}
	}

	printk("<-- END_KERNEL_%s_TESTS -->\n", suite_name);
}

#endif // ROS_INC_KTEST_H
