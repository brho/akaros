#include <arch/arch.h>
#include <ros/time.h>
#include <stdio.h>
#include <schedule.h>
#include <multiboot.h>
#include <pmap.h>
#include <smp.h>

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

/* Convenience wrapper called when a core's timer interrupt goes off.  Not to be
 * confused with global timers (like the PIC).  Do not put your code here.  If
 * you want something to happen in the future, set an alarm. */
void timer_interrupt(struct trapframe *tf, void *data)
{
	struct timer_chain *pcpui_tchain = &per_cpu_info[core_id()].tchain;
	trigger_tchain(pcpui_tchain);
}

/* We can overflow/wraparound when we multiply up, but we have to divide last,
 * or else we lose precision.  If we're too big and will overflow, we'll
 * sacrifice precision for correctness, and degrade to the next lower level
 * (losing 3 digits worth).  The recursive case shouldn't overflow, since it
 * called something that scaled down the tsc_time by more than 1000. */
uint64_t tsc2sec(uint64_t tsc_time)
{
	return tsc_time / system_timing.tsc_freq;
}

uint64_t tsc2msec(uint64_t tsc_time)
{
	if (mult_will_overflow_u64(tsc_time, 1000))
		return tsc2sec(tsc_time) * 1000;
	else 
		return (tsc_time * 1000) / system_timing.tsc_freq;
}

uint64_t tsc2usec(uint64_t tsc_time)
{
	if (mult_will_overflow_u64(tsc_time, 1000000))
		return tsc2msec(tsc_time) * 1000;
	else
		return (tsc_time * 1000000) / system_timing.tsc_freq;
}

uint64_t tsc2nsec(uint64_t tsc_time)
{
	if (mult_will_overflow_u64(tsc_time, 1000000000))
		return tsc2usec(tsc_time) * 1000;
	else
		return (tsc_time * 1000000000) / system_timing.tsc_freq;
}

uint64_t sec2tsc(uint64_t sec)
{
	if (mult_will_overflow_u64(sec, system_timing.tsc_freq)) {
		/* in this case, we simply can't express the number of ticks */
		warn("Wraparound in sec2tsc(), rounding up");
		return (uint64_t)(-1);
	} else {
		return sec * system_timing.tsc_freq;
	}
}

uint64_t msec2tsc(uint64_t msec)
{
	if (mult_will_overflow_u64(msec, system_timing.tsc_freq))
		return sec2tsc(msec / 1000);
	else
		return (msec * system_timing.tsc_freq) / 1000;
}

uint64_t usec2tsc(uint64_t usec)
{
	if (mult_will_overflow_u64(usec, system_timing.tsc_freq))
		return msec2tsc(usec / 1000);
	else
		return (usec * system_timing.tsc_freq) / 1000000;
}

uint64_t nsec2tsc(uint64_t nsec)
{
	if (mult_will_overflow_u64(nsec, system_timing.tsc_freq))
		return usec2tsc(nsec / 1000);
	else
		return (nsec * system_timing.tsc_freq) / 1000000000;
}
