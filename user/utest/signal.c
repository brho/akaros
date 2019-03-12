#include <utest/utest.h>
#include <pthread.h>

TEST_SUITE("SIGNALS");

/* <--- Begin definition of test cases ---> */

bool test_sigmask(void)
{
	int count = 0;
	pthread_t sigphandle;
	void *thread_handler(void *arg)
	{
		sigset_t s;
		sigemptyset(&s);
		sigaddset(&s, SIGUSR2);
		pthread_sigmask(SIG_BLOCK, &s, NULL);
		for (int i=0; i<100000; i++)
			pthread_yield();
		return 0;
	}
	void signal_handler(int signo)
	{
		sigphandle = pthread_self();
		__sync_fetch_and_add(&count, 1);
	}

	struct sigaction sigact = {.sa_handler = signal_handler, 0};
	sigaction(SIGUSR1, &sigact, 0);
	sigaction(SIGUSR2, &sigact, 0);

	pthread_t phandle;
	pthread_create(&phandle, NULL, thread_handler, NULL);
	for (int i=0; i<100; i++)
		pthread_yield();
	pthread_kill(phandle, SIGUSR1);
	pthread_kill(phandle, SIGUSR2);
	pthread_join(phandle, NULL);

	UT_ASSERT_M("Should only receive one signal", count == 1); 
	UT_ASSERT_M("Signal handler run on wrong thread", sigphandle ==
		    phandle); 
	return true;
}

/* <--- End definition of test cases ---> */

struct utest utests[] = {
	UTEST_REG(sigmask),
};
int num_utests = sizeof(utests) / sizeof(struct utest);

int main(int argc, char *argv[]) {
	char **whitelist = &argv[1];
	int whitelist_len = argc - 1;

	RUN_TEST_SUITE(utests, num_utests, whitelist, whitelist_len);
}


