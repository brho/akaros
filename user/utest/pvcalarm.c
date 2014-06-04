#include <utest.h>
#include <pthread.h>
#include <pvcalarm.h>

TEST_SUITE("PVCALARMS");

/* <--- Begin definition of test cases ---> */
bool test_pvcalarms(void) {
	const int INTERVAL = 10000;
	const int ITERATIONS = 100;
	int count[max_vcores()];
	void pvcalarm_callback()
	{
		__sync_fetch_and_add(&count[vcore_id()], 1);
	}

	pthread_lib_init();
	pthread_can_vcore_request(FALSE);
	vcore_request(max_vcores() - num_vcores());
	for (int i=0; i<max_vcores(); i++)
		count[i] = 0;
	
	uint64_t now, then;
	now = tsc2usec(read_tsc());
	enable_pvcalarms(PVCALARM_PROF, INTERVAL, pvcalarm_callback);
	for (int i=0; i<max_vcores(); i++)
		while(count[i] < ITERATIONS)
			cpu_relax();
	then = tsc2usec(read_tsc());
	disable_pvcalarms();

	UT_ASSERT_M("Alarms finished too soon", then > (now + INTERVAL*count[0]));
	UT_ASSERT_M("Alarms finished too late", then < (now + 2*INTERVAL*count[0]));
	return true;
}

/* <--- End definition of test cases ---> */

struct utest utests[] = {
	UTEST_REG(pvcalarms),
};
int num_utests = sizeof(utests) / sizeof(struct utest);

int main(int argc, char *argv[]) {
	// Run test suite passing it all the args as whitelist of what tests to run.
	char **whitelist = &argv[1];
	int whitelist_len = argc - 1;
	RUN_TEST_SUITE(utests, num_utests, whitelist, whitelist_len);
}
