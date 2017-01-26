#include <utest/utest.h>
#include <pthread.h>
#include <benchutil/pvcalarm.h>

TEST_SUITE("PVCALARMS");

/* <--- Begin definition of test cases ---> */
bool test_pvcalarms(void) {
	const int INTERVAL = 10000;
	const int ITERS = 100;
	int count[max_vcores()];
	void pvcalarm_callback()
	{
		__sync_fetch_and_add(&count[vcore_id()], 1);
	}

	parlib_never_yield = TRUE;
	pthread_mcp_init();
	vcore_request_total(max_vcores());
	parlib_never_vc_request = TRUE;
	for (int i=0; i<max_vcores(); i++)
		count[i] = 0;
	
	uint64_t now, then;
	now = tsc2usec(read_tsc());
	enable_pvcalarms(PVCALARM_PROF, INTERVAL, pvcalarm_callback);
	for (int i=0; i<max_vcores(); i++)
		while(count[i] < ITERS)
			cpu_relax();
	disable_pvcalarms();
	then = tsc2usec(read_tsc());

	UT_ASSERT_M("Alarms finished too soon", then > (now + INTERVAL*ITERS));
	UT_ASSERT_M("Alarms finished too late", then < (now + 2*INTERVAL*ITERS));
	return true;
}

bool test_sigperf(void)
{
	const int INTERVAL = 10000;
	const int ITERATIONS = 100;
	const int NUM_PTHREADS = 10;
	int count[NUM_PTHREADS];
	pthread_t threads[NUM_PTHREADS];
	static __thread int *__count;

	void *thread_handler(void *arg)
	{
		__count = (int*)arg;
		sigset_t s;
		sigemptyset(&s);
		sigaddset(&s, SIGPROF);
		pthread_sigmask(SIG_UNBLOCK, &s, NULL);
		int old_count = 0, new_count = 0;
		while(1) {
			while((new_count = atomic_read((atomic_t)__count)) <= old_count);
			if (new_count >= ITERATIONS) break;
			old_count = new_count;
			pthread_yield();
		}
		return 0;
	}
	void signal_handler(int signo)
	{
		assert(signo == SIGPROF);
		__sync_fetch_and_add(__count, 1);
	}

	pthread_lib_init();
	parlib_never_yield = FALSE;
	parlib_never_vc_request = FALSE;

	sigset_t s;
	sigemptyset(&s);
	sigaddset(&s, SIGPROF);
	pthread_sigmask(SIG_BLOCK, &s, NULL);
	struct sigaction sigact = {.sa_handler = signal_handler, 0};
	sigaction(SIGPROF, &sigact, 0);
	for (int i=0; i<NUM_PTHREADS; i++)
		count[i] = 0;

	enable_profalarm(INTERVAL);
	for (int i=0; i<NUM_PTHREADS; i++)
		pthread_create(&threads[i], NULL, thread_handler, &count[i]);

	for (int i=0; i<NUM_PTHREADS; i++)
		while(count[i] < ITERATIONS)
			cpu_relax();

	disable_pvcalarms();
	return true;
}

/* <--- End definition of test cases ---> */

struct utest utests[] = {
	UTEST_REG(pvcalarms),
	UTEST_REG(sigperf),
};
int num_utests = sizeof(utests) / sizeof(struct utest);

int main(int argc, char *argv[]) {
	// Run test suite passing it all the args as whitelist of what tests to run.
	char **whitelist = &argv[1];
	int whitelist_len = argc - 1;
	RUN_TEST_SUITE(utests, num_utests, whitelist, whitelist_len);
}
