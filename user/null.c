// null app
#include <inc/lib.h>
#include <inc/types.h>
#include <inc/syscall.h>
#include <inc/x86.h>

uint64_t avg(uint32_t (COUNT(length) array)[], int length) 
{
	uint64_t sum = 0;
	for(int i=0; i<length; i++) {
		sum+=array[i];
	}
	return (length > 0) ? sum/((uint64_t)length) : 0;
}

void umain(void)
{
	//Get the measurements a bunch of times to make sure its accurate
	#define NUM_ITERATIONS	500
	uint32_t times[NUM_ITERATIONS];
	for(int i=0; i<NUM_ITERATIONS; i++) {
		//times[i] = get_time();
		sys_null();
		//times[i] = get_time() - times[i];
	}
	
	//Compute the average and print it
	uint64_t a = avg(times, NUM_ITERATIONS);
	cprintf_async("Average latency: %ld", a);
	//cprintf("Standard Deviation: %d", stddev);

	//Spin to make sure we don't have any resources dealocated before done
	while(1);
}
