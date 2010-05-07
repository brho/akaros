
// zra: why is this in the kernel?

#ifdef __SHARC__
#pragma nosharc
#endif

#include <arch/arch.h>
#include <ros/timer.h>
#include <stdio.h>
#include <schedule.h>
<<<<<<< HEAD
=======
#include <multiboot.h>
#include <pmap.h>
#include <arch/perfmon.h>
>>>>>>> 3611594... adding support for perfctr in trad_proc timer handler

/* timing_overhead
 * Any user space process that links to this file will get its own copy.  
 * This means it will manually have to call tune_timing itself before it 
 * makes its first measurement.
 */
uint64_t timing_overhead = 0;

/* start_timing()
 * This function simply reads the tsc in a serialized fashion and returns its
 * value.  It is pusposefully annotated with a noinline so that the overheads 
 * assocaited with calling it are as deterministic as possible.
 */
uint64_t start_timing()
{
    return read_tsc_serialized();
}

/* stop_timing()
 * This function reads the tsc in a serialized fashion and subtracts the value
 * it reads from the value passed in as a paramter in order to determine the 
 * difference between the two values.  A global timing_overhead value is also 
 * subtracted to compensate for the overhead associated with calling both
 * start and stop timing and returning their values.
 * This function is purposefully annotated with a noinline so that 
 * the overheads assocaited with calling it are as deterministic as possible.
 */
uint64_t stop_timing(uint64_t val)
{
    uint64_t diff = (read_tsc_serialized() - val - timing_overhead);
	if ((int64_t) diff < 0) 
		return 1;
	return diff;
}

/* train_timing()
 * This function is intended to train the timing_overhead variable for use by
 * stop_timing().  It runs through a loop calling start/stop and averaging the 
 * overhead of calling them without doing any useful work in between.
 */
void train_timing() 
{
	int i;
	// set training overhead to be something large
	register uint64_t training_overhead = 0xffffffff;
	register uint64_t time, diff;

	//Do this 3 times outside the loop to warm up cpuid
	time = start_timing();
 	diff = stop_timing(time);
	time = start_timing();
 	diff = stop_timing(time);
	time = start_timing();
 	diff = stop_timing(time);
	for(i=0; i<10000; i++) {
		time = start_timing();
 		diff = stop_timing(time);
		
		/* In case diff was negative, I want to add its absolute value
		 * to the cumulative error, otherwise, just diff itself
		 */
		if((int64_t)diff < 0)
			diff = (uint64_t)(~0) - diff + 1;
		training_overhead = MIN(training_overhead, diff);
	}
	timing_overhead = training_overhead;
}

/* Typical per-core timer interrupt handler.  Note that sparc's timer is
 * periodic by nature, so if you want it to not be periodic, turn off the alarm
 * in here. */
void timer_interrupt(struct trapframe *tf, void *data)
{
#ifdef __CONFIG_EXPER_TRADPROC__

	#ifdef __sparc_v8__
	# define num_misses read_perfctr(core_id(),22)
	#else
	# define num_misses (read_pmc(1))
	#endif

	// cause M misses and run for N usec
	#define M 1000
	#define N 10
	unsigned int lfsr = 1+read_tsc()%7;

	uint64_t t0 = read_tsc();
	uint64_t misses0 = num_misses;
	while(num_misses-misses0 < M)
	{
		int x;
		x = *(volatile int*)KADDR((4*lfsr) % maxaddrpa);
		lfsr = (lfsr >> 1) ^ (unsigned int)(0 - ((lfsr & 1u) & 0xd0000001u));
	}
	while((read_tsc()-t0)/(system_timing.tsc_freq/1000000) < N);

	/* about every 10 ticks (100ms) run the load balancer.  Offset by coreid so
	 * it's not as horrible.  */
	if (per_cpu_info[core_id()].ticks % 10 == core_id())
		load_balance();
	local_schedule();
#endif /* __CONFIG_EXPER_TRADPROC__ */
}
