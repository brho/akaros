#include <utest/utest.h>
#include <pthread.h>

TEST_SUITE("PTHREADS");

/* <--- Begin definition of test cases ---> */

bool test_mutex_null_attr(void)
{
	pthread_mutex_t mu;
	int ret;

	ret = pthread_mutex_init(&mu, 0);
	UT_ASSERT(ret == 0);
	return TRUE;
}

/* <--- End definition of test cases ---> */

struct utest utests[] = {
	UTEST_REG(mutex_null_attr),
};
int num_utests = sizeof(utests) / sizeof(struct utest);

int main(int argc, char *argv[])
{
	// Run test suite passing it all the args as whitelist of what tests to run.
	char **whitelist = &argv[1];
	int whitelist_len = argc - 1;

	RUN_TEST_SUITE(utests, num_utests, whitelist, whitelist_len);
}
