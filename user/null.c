// null app
#include <inc/lib.h>
#include <inc/types.h>
#include <inc/syscall.h>
#include <inc/x86.h>
#include <inc/measure.h>
#include <inc/null.h>
#include <inc/timer.h>

#ifdef __DEPUTY__
#pragma nodeputy
#endif

#define NUM_ITERATIONS	100000
uint64_t times[NUM_ITERATIONS];

uint64_t total(uint64_t (COUNT(length) array)[], int length)
{
	uint64_t sum = 0;
	for(int i=0; i<length; i++) {
		sum+=array[i];
	}
	return sum;
	//return (length > 0) ? sum/((uint64_t)length) : 0;
}

void print_timer(timer_t* timer)
{
	cprintf("%s, %llu, %llu\n", timer->label, timer->curr_run, timer->aggr_run);
}

void umain(void)
{
	extern uint64_t timing_overhead;
	async_desc_t *desc1, *desc2;
	async_rsp_t rsp1, rsp2;
	cprintf ("training result %llu\n", timing_overhead);
	/*	
	cache_buster_async(&desc1, 20, 0xdeadbeef);
	cache_buster_async(&desc2, 10, 0xcafebabe);
	waiton_async_call(desc1, &rsp1);
	waiton_async_call(desc2, &rsp2);
	//measure_function(sys_null(), NUM_ITERATIONS, "sys_null");
	//measure_function(asm volatile("nop;"), NUM_ITERATIONS, "nop");
	//measure_function(cprintf("Reg Sync call  \n"), 10, "printf");
	//measure_function_async(cprintf_async(&desc, "Cross-Core call\n"), desc, 10,\
	//                       1, "Async Printf");
	// Note this won't actually do 100 inner runs (first parameter).  will stop
	// making calls beyond the ring size and won't wait on any extra calls.
	measure_async_call(null_async(&desc), desc, 100, 100, "Async Null");
	*/
	for (int i=0; i<100;i++){
		TAGGED_TIMING_BEGIN(umain);
		TAGGED_TIMING_END(umain);
	}
	POOL_FOR_EACH(&timer_pool, print_timer);
	// Spin to make sure we don't have any resources deallocated before done
	while(1);
}
