/* Copyright (c) 2014 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Basic pthread switcher, bypassing the 2LS.  Use for benchmarking and
 * 2LS-inspiration. */

#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include "misc-compat.h"

pthread_t th1, th2;
int nr_switch_loops = 100;
bool ready = FALSE;
bool should_exit = FALSE;

static void __pth_switch_cb(struct uthread *uthread, void *target)
{
	/* by not returning, this bypasses vcore entry and event checks, though when
	 * we pop back out of the 2LS, we'll check notif pending.  think about this
	 * if you put this into your 2LS. */
	current_uthread = NULL;
	run_uthread((struct uthread*)target);
	assert(0);
}

static void pth_switch_to(struct pthread_tcb *target)
{
	uthread_yield(TRUE, __pth_switch_cb, target);
}

void *switch_thread(void *arg)
{	
	pthread_t other_thr = *(pthread_t*)arg;

	while (!ready)
		cpu_relax();
	for (int i = 0; i < nr_switch_loops; i++) {
		cmb();
		if (should_exit)
			return 0;
		pth_switch_to(other_thr);
	}
	if (pthread_self() == th1) {
		/* we need to break out of the switching cycle.  when th2 runs again,
		 * it'll know to stop.  but th1 needs to both exit and switch to th2.
		 * we do this by making th2 runnable by the pth schedop, then exiting */
		should_exit = TRUE;
		/* we also need to do this to th2 before it tries to exit, o/w we'll PF
		 * in __pthread_generic_yield. */
		sched_ops->thread_runnable((struct uthread*)th2);
	}
	return 0;
}

int main(int argc, char** argv) 
{
	struct timeval start_tv = {0};
	struct timeval end_tv = {0};
	long usec_diff;
	long nr_ctx_switches;
	void *join_ret;

	if (argc > 1)
		nr_switch_loops = strtol(argv[1], 0, 10);
	printf("Making 2 threads of %d switches each\n", nr_switch_loops);

	pthread_can_vcore_request(FALSE);	/* 2LS won't manage vcores */
	pthread_need_tls(FALSE);
	pthread_lib_init();					/* gives us one vcore */

	/* each is passed the other's pthread_t.  th1 starts the switching. */
	if (pthread_create(&th1, NULL, &switch_thread, &th2))
		perror("pth_create 1 failed");
	/* thread 2 is created, but not put on the runnable list */
	if (__pthread_create(&th2, NULL, &switch_thread, &th1))
		perror("pth_create 2 failed");

	if (gettimeofday(&start_tv, 0))
		perror("Start time error...");

	ready = TRUE;			/* signal to any spinning uthreads to start */

	pthread_join(th1, &join_ret);
	pthread_join(th2, &join_ret);

	if (gettimeofday(&end_tv, 0))
		perror("End time error...");
	nr_ctx_switches = 2 * nr_switch_loops;
	usec_diff = (end_tv.tv_sec - start_tv.tv_sec) * 1000000 +
	            (end_tv.tv_usec - start_tv.tv_usec);
	printf("Done: %d loops\n", nr_switch_loops);
	printf("Nr context switches: %ld\n", nr_ctx_switches);
	printf("Time to run: %ld usec\n", usec_diff);
	printf("Context switch latency: %d nsec\n",
	       (int)(1000LL*usec_diff / nr_ctx_switches));
	printf("Context switches / sec: %d\n\n",
	       (int)(1000000LL*nr_ctx_switches / usec_diff));
} 
