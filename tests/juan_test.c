#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>

/* OS dependent #incs */
#include <parlib.h>
#include <vcore.h>
#include <timing.h>

static uint32_t __get_pcoreid(void)
{
	return __procinfo.vcoremap[vcore_id()].pcoreid;
}

static __attribute__ ((noinline)) int juan_work(void)
{
    const int MAX_ITER = 100000;
    register int res = 0;
    for (int i = 0; i < MAX_ITER; ++i) {
        for (int j = 0; j < i; ++j) {
            res += (i * 2 - 5 * j) / 3;
        }
    }
	return res;
}

static void juan_test(void)
{
	unsigned long long usec_diff;
	struct timeval start_tv = {0};
	struct timeval end_tv = {0};
	int res;

	printf("We are %sin MCP mode, running on vcore %d, pcore %d\n",
	       (in_multi_mode() ? "" : "not "), vcore_id(),
	       __get_pcoreid());

	if (gettimeofday(&start_tv, 0))
		perror("Start time error...");

	res = juan_work();

	if (gettimeofday(&end_tv, 0))
		perror("End time error...");

	usec_diff = (end_tv.tv_sec - start_tv.tv_sec) * 1000000 +
	            (end_tv.tv_usec - start_tv.tv_usec);

    printf("Result: %d Usec diff: %llu\n", res, usec_diff);
}

void *juan_thread(void* arg)
{	
	juan_test();
}

int main(int argc, char** argv) 
{
	pthread_t child;
	void *child_ret;
	juan_test();
	printf("Spawning thread, etc...\n");
	pthread_create(&child, NULL, &juan_thread, NULL);
	pthread_join(child, &child_ret);
} 
