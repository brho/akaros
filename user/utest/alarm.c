#include <utest/utest.h>
#include <parlib/alarm.h>

TEST_SUITE("ALARMS");

/* <--- Begin definition of test cases ---> */

bool test_alarm(void) {
	const int INTERVAL = 10000;
	const int ITERATIONS = 100;
	void alarm_handler(struct alarm_waiter *waiter)
	{
		__sync_fetch_and_add((int*)waiter->data, 1);
		set_awaiter_inc(waiter, INTERVAL);
		set_alarm(waiter);
	}

	int count = 0;
	uint64_t now, then;
	struct alarm_waiter waiter;
	init_awaiter(&waiter, alarm_handler);
	waiter.data = &count;
	set_awaiter_rel(&waiter, INTERVAL);
	now = tsc2usec(read_tsc());
	set_alarm(&waiter);
	while(count < ITERATIONS)
		cpu_relax();
	then = tsc2usec(read_tsc());
	unset_alarm(&waiter);
	UT_ASSERT_M("Alarms finished too soon", then >= (now + INTERVAL*count));
	UT_ASSERT_M("Alarms finished too late", then < (now + 2*INTERVAL*count));
	return true;
}

/* <--- End definition of test cases ---> */

struct utest utests[] = {
	UTEST_REG(alarm),
};
int num_utests = sizeof(utests) / sizeof(struct utest);

int main(int argc, char *argv[]) {
	char **whitelist = &argv[1];
	int whitelist_len = argc - 1;
	RUN_TEST_SUITE(utests, num_utests, whitelist, whitelist_len);
}


