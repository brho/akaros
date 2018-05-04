#include <stdbool.h>
#include <ktest.h>
#include <sys/queue.h>

/* Global string used to report info about the last completed test */
char ktest_msg[1024];

/* Global linked list used to store registered ktest suites */
SLIST_HEAD(suiteq, ktest_suite);
static struct suiteq ktest_suiteq = SLIST_HEAD_INITIALIZER(ktest_suiteq);

void register_ktest_suite(struct ktest_suite *suite)
{
	SLIST_INSERT_HEAD(&ktest_suiteq, suite, link);
}

void run_registered_ktest_suites()
{
	struct ktest_suite *suite = NULL;
	SLIST_FOREACH(suite, &ktest_suiteq, link) {
		run_ktest_suite(suite);
	}
}

void run_ktest_suite(struct ktest_suite *suite)
{
	printk("<-- BEGIN_KERNEL_%s_TESTS -->\n", suite->name);

	for (int i=0; i<suite->num_ktests; i++) {
		struct ktest *test = &suite->ktests[i];
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
			/* Some older tests disable IRQs */
			enable_irq();
		} else {
			printk("\tDISABLED [%s]\n", test->name);
		}
	}

	printk("<-- END_KERNEL_%s_TESTS -->\n", suite->name);
}

