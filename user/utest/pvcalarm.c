#include <utest.h>
#include <vcore.h>
#include <uthread.h>
#include <event.h>
#include <pvcalarm.h>

TEST_SUITE("PVCALARMS");

void pvcalarm_vcore_entry()
{
	uint32_t vcoreid= vcore_id();

	/* Drop back into main thread  for core 0 */
	if (current_uthread) {
		assert(vcoreid == 0);
		run_current_uthread();
	}

	/* Other vcores get here, so enable notifs so they can get their alarm
 	 * events */
	enable_notifs(vcoreid);
	while(1);
}
struct schedule_ops pvcalarm_sched_ops = {
	.sched_entry = pvcalarm_vcore_entry,
};
struct schedule_ops *sched_ops = &pvcalarm_sched_ops;

/* <--- Begin definition of test cases ---> */
bool test_pvcalarms(void) {
	const int INTERVAL = 10000;
	const int ITERATIONS = 100;
	int count[max_vcores() - num_vcores()];
	void pvcalarm_callback()
	{
		__sync_fetch_and_add(&count[vcore_id()], 1);
	}

	struct uthread dummy = {0};
	uthread_lib_init(&dummy);
	vcore_request(max_vcores() - num_vcores());
	
	uint64_t now, then;
	now = tsc2usec(read_tsc());
	enable_pvcalarms(PVCALARM_PROF, INTERVAL, pvcalarm_callback);
	for (int i=0; i<num_vcores(); i++)
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
