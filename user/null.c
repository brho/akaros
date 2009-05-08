// null app
#include <inc/lib.h>
#include <inc/types.h>
#include <inc/syscall.h>
#include <inc/x86.h>
#include <inc/measure.h>
#include <inc/null.h>

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

void umain(void)
{
	measure_function(sys_null(), NUM_ITERATIONS, "sys_null");
	measure_function(asm volatile("nop;"), NUM_ITERATIONS, "nop");
	//measure_function(cprintf("Reg Sync call  \n"), 10, "printf");
	//measure_function_async(cprintf_async(&desc, "Cross-Core call\n"), desc, 10,\
	//                       1, "Async Printf");
	measure_async_call(null_async(&desc), desc, 10, 1, "Async Null");

	// Spin to make sure we don't have any resources deallocated before done
	while(1);
}
