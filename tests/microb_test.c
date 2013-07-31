/* Copyright (c) 2013 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Basic perf test for small functions.  Will run them in a loop and give you
 * the average cost per iteration.  It'll run them both as an SCP and an MCP.
 *
 * To use this, define a function of the form:
 *
 * 		void my_test(unsigned long nr_loops)
 *
 * Which does some computation you wish to measure inside a loop that run
 * nr_loops times.  Then in microb_test(), add your function in a line such as:
 *
 * 		test_time_ns(my_test, 100000);
 *
 * This macro will run your test and print the results.  Pick a loop amount that
 * is reasonable for your operation.  You can also use test_time_us() for longer
 * operations.
 *
 * Notes:
 * - I went with this style so you could do some prep work before and after the
 *   loop (instead of having a macro build the loop).  It's what I needed.
 * - Be sure to double check the ASM inside the loop to make sure the compiler
 *   isn't optimizing out your main work. 
 * - Make sure your function isn't static.  If it is static (and even if it is
 *   __attribute__((noinline))), if the function is called only once, the
 *   compiler will compile it differently (specifically, it will hardcode the
 *   number of loops into the function, instead of taking a parameter).
 *   Suddenly, the existence of a second test of the same function could change
 *   the performance of *both* test runs.  Incidentally, when this happened to
 *   me, the tests were *better* when this optimization didn't happen.  The way
 *   to avoid the optimization completely is to have extern functions, since the
 *   compiler can't assume it is only called once.  Though technically they
 *   still could do some optimizations, and the only really safe way is to put
 *   the tests in another .c file. */

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

/* Testing functions here */

void set_tlsdesc_test(unsigned long nr_loops)
{
#ifdef __i386__
	uint32_t vcoreid = vcore_id();
	void *mytls = get_tls_desc(vcoreid);
	void *vctls = get_vcpd_tls_desc(vcoreid);
	segdesc_t tmp = SEG(STA_W, (uint32_t)vctls, 0xffffffff, 3);
	uint32_t gs = (vcoreid << 3) | 0x07;
    for (int i = 0; i < nr_loops; i++) {
		__procdata.ldt[vcoreid] = tmp;
		cmb();
		asm volatile("movl %0,%%gs" : : "r" (gs) : "memory");
    }
	set_tls_desc(mytls, vcoreid);
#endif
}

/* Internal test infrastructure */

void loop_overhead(unsigned long nr_loops)
{
    for (int i = 0; i < nr_loops; i++) {
		cmb();
    }
}

/* Runs func(loops) and returns the usec elapsed */
#define __test_time_us(func, loops)                                            \
({                                                                             \
	struct timeval start_tv = {0};                                             \
	struct timeval end_tv = {0};                                               \
	if (gettimeofday(&start_tv, 0))                                            \
		perror("Start time error...");                                         \
	(func)((loops));                                                           \
	if (gettimeofday(&end_tv, 0))                                              \
		perror("End time error...");                                           \
	((end_tv.tv_sec - start_tv.tv_sec) * 1000000 +                             \
	 (end_tv.tv_usec - start_tv.tv_usec));                                     \
})

/* Runs func(loops) and returns the nsec elapsed */
#define __test_time_ns(func, loops)                                            \
({                                                                             \
	(__test_time_us((func), (loops)) * 1000);                                  \
})

/* Runs func(loops), subtracts the loop overhead, and prints the result */
#define test_time_us(func, loops)                                              \
({                                                                             \
	unsigned long long usec_diff;                                              \
	usec_diff = __test_time_us((func), (loops)) - nsec_per_loop * loops / 1000;\
	printf("\"%s\" total: %lluus, per iteration: %lluus\n", #func, usec_diff,  \
	       usec_diff / (loops));                                               \
})

/* Runs func(loops), subtracts the loop overhead, and prints the result */
#define test_time_ns(func, loops)                                              \
({                                                                             \
	unsigned long long nsec_diff;                                              \
	nsec_diff = __test_time_ns((func), (loops)) - nsec_per_loop * (loops);     \
	printf("\"%s\" total: %lluns, per iteration: %lluns\n", #func, nsec_diff,  \
	       nsec_diff / (loops));                                               \
})

static void microb_test(void)
{
	unsigned long long nsec_per_loop;
	printf("We are %sin MCP mode, running on vcore %d, pcore %d\n",
	       (in_multi_mode() ? "" : "not "), vcore_id(),
	       __get_pcoreid());
	/* We divide the overhead by loops, and later we multiply again, which drops
	 * off some accuracy at the expense of usability (can do different
	 * iterations for different tests without worrying about recomputing the
	 * loop overhead). */
	nsec_per_loop = __test_time_ns(loop_overhead, 100000) / 100000;
	printd("Loop overhead per loop: %lluns\n", nsec_per_loop);

	/* Add your tests here.  Func name, number of loops */
	test_time_ns(set_tlsdesc_test , 100000);
}

void *worker_thread(void* arg)
{	
	microb_test();
	return 0;
}

int main(int argc, char** argv) 
{
	pthread_t child;
	void *child_ret;
	microb_test();
	printf("Spawning worker thread, etc...\n");
	pthread_create(&child, NULL, &worker_thread, NULL);
	pthread_join(child, &child_ret);
} 
