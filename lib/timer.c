#include <inc/x86.h>
#include <inc/timer.h>

// TODO: export timing_overhead to user level 

uint64_t read_tsc_serialized() __attribute__((noinline)) 
{
	cpuid(0, 0, 0, 0, 0);
	return read_tsc();
}

uint64_t start_timing() __attribute__((noinline)) 
{
	return read_tsc_serialized();
}

uint64_t stop_timing(uint64_t val, uint64_t overhead) __attribute__((noinline)) 
{
	return (read_tsc_serialized() - val - overhead);
}
