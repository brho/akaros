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

int main(int argc, char** argv)
{
	/*
	TAGGED_TIMING_BEGIN(tst);
	async_desc_t *desc1, *desc2;
	async_rsp_t rsp1, rsp2;
	cprintf ("training result %llu\n", timing_overhead);
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
	for (int i=0; i<100;i++){
		TAGGED_TIMING_BEGIN(umain);
		TAGGED_TIMING_END(umain);
	}
	POOL_FOR_EACH(&timer_pool, print_timer);
	// Spin to make sure we don't have any resources deallocated before done
	while(1);
	*/
	cprintf("Env %x says hi and quits\n", env->env_id);
}
