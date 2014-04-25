/* Copyright (c) 2010-14 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Basic test for pthreading.  Spawns a bunch of threads that yield.
 *
 * To build on linux, cd into tests and run:
 * $ gcc -O2 -std=gnu99 -fno-stack-protector -g pthread_test.c -lpthread
 *
 * Make sure you run it with taskset to fix the number of vcores/cpus. */

#define _GNU_SOURCE /* for pth_yield on linux */

#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include "misc-compat.h" /* OS dependent #incs */

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
#define printf_safe(...) {}
//#define printf_safe(...) \
	pthread_mutex_lock(&lock); \
	printf(__VA_ARGS__); \
	pthread_mutex_unlock(&lock);

#define MAX_NR_TEST_THREADS 100000
int nr_yield_threads = 100;
int nr_yield_loops = 100;
int nr_vcores = 0;
int amt_fake_work = 0;

pthread_t my_threads[MAX_NR_TEST_THREADS];
void *my_retvals[MAX_NR_TEST_THREADS];

bool ready = FALSE;

void *yield_thread(void* arg)
{	
	/* Wait til all threads are created */
	while (!ready)
		cpu_relax();
	for (int i = 0; i < nr_yield_loops; i++) {
		printf_safe("[A] pthread %d %p on vcore %d, itr: %d\n", pthread_id(),
		            pthread_self(), vcore_id(), i);
		/* Fakes some work by spinning a bit.  Amount varies per uth/vcore,
		 * scaled by fake_work */
		if (amt_fake_work)
			udelay(amt_fake_work * (pthread_id() * (vcore_id() + 2)));
		pthread_yield();
		printf_safe("[A] pthread %p returned from yield on vcore %d, itr: %d\n",
		            pthread_self(), vcore_id(), i);
	}
	return (void*)(pthread_self());
}

int main(int argc, char** argv) 
{
	struct timeval start_tv = {0};
	struct timeval end_tv = {0};
	long usec_diff;
	long nr_ctx_switches;

	if (argc > 1)
		nr_yield_threads = strtol(argv[1], 0, 10);
	if (argc > 2)
		nr_yield_loops = strtol(argv[2], 0, 10);
	if (argc > 3)
		nr_vcores = strtol(argv[3], 0, 10);
	if (argc > 4)
		amt_fake_work = strtol(argv[4], 0, 10);
	nr_yield_threads = MIN(nr_yield_threads, MAX_NR_TEST_THREADS);
	printf("Making %d threads of %d loops each, on %d vcore(s), %d work\n",
	       nr_yield_threads, nr_yield_loops, nr_vcores, amt_fake_work);

	/* OS dependent prep work */
#ifdef __ros__
	if (nr_vcores) {
		/* Only do the vcore trickery if requested */
		pthread_can_vcore_request(FALSE);	/* 2LS won't manage vcores */
		pthread_need_tls(FALSE);
		pthread_lib_init();					/* gives us one vcore */
		vcore_request(nr_vcores - 1);		/* ghetto incremental interface */
		for (int i = 0; i < nr_vcores; i++) {
			printf_safe("Vcore %d mapped to pcore %d\n", i,
			            __procinfo.vcoremap[i].pcoreid);
		}
	}
#endif /* __ros__ */

	/* create and join on yield */
	for (int i = 0; i < nr_yield_threads; i++) {
		printf_safe("[A] About to create thread %d\n", i);
		if (pthread_create(&my_threads[i], NULL, &yield_thread, NULL))
			perror("pth_create failed");
	}
	if (gettimeofday(&start_tv, 0))
		perror("Start time error...");
	ready = TRUE;			/* signal to any spinning uthreads to start */
	for (int i = 0; i < nr_yield_threads; i++) {
		printf_safe("[A] About to join on thread %d(%p)\n", i, my_threads[i]);
		pthread_join(my_threads[i], &my_retvals[i]);
		printf_safe("[A] Successfully joined on thread %d (retval: %p)\n", i,
		            my_retvals[i]);
	}
	if (gettimeofday(&end_tv, 0))
		perror("End time error...");
	nr_ctx_switches = nr_yield_threads * nr_yield_loops;
	usec_diff = (end_tv.tv_sec - start_tv.tv_sec) * 1000000 +
	            (end_tv.tv_usec - start_tv.tv_usec);
	printf("Done: %d uthreads, %d loops, %d vcores, %d work\n",
	       nr_yield_threads, nr_yield_loops, nr_vcores, amt_fake_work);
	printf("Nr context switches: %ld\n", nr_ctx_switches);
	printf("Time to run: %ld usec\n", usec_diff);
	if (nr_vcores == 1)
		printf("Context switch latency: %d nsec\n",
		       (int)(1000LL*usec_diff / nr_ctx_switches));
	printf("Context switches / sec: %d\n\n",
	       (int)(1000000LL*nr_ctx_switches / usec_diff));
} 
