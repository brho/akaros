#include <arch/arch.h>
#include <time.h>
#include <stdio.h>
#include <schedule.h>
#include <multiboot.h>
#include <pmap.h>
#include <smp.h>
#include <ros/procinfo.h>

/* Determines the overhead of tsc timing.  Note the start/stop calls are
 * inlined, so we're trying to determine the lowest amount of overhead
 * attainable by using the TSC (or whatever timing source).
 *
 * For more detailed TSC measurements, use test_rdtsc() in k/a/i/rdtsc_test.c */
void train_timing() 
{
	uint64_t min_overhead = UINT64_MAX;
	uint64_t max_overhead = 0;
	uint64_t time, diff;
	int8_t irq_state = 0;

	/* Reset this, in case we run it again.  The use of start/stop to determine
	 * the overhead relies on timing_overhead being 0. */
	__proc_global_info.tsc_overhead = 0;
	/* timing might use cpuid, in which case we warm it up to avoid some extra
	 * variance */
	time = start_timing();
 	diff = stop_timing(time);
	time = start_timing();
 	diff = stop_timing(time);
	time = start_timing();
 	diff = stop_timing(time);
	disable_irqsave(&irq_state);
	for (int i = 0; i < 10000; i++) {
		time = start_timing();
 		diff = stop_timing(time);
		min_overhead = MIN(min_overhead, diff);
		max_overhead = MAX(max_overhead, diff);
	}
	enable_irqsave(&irq_state);
	__proc_global_info.tsc_overhead = min_overhead;
	printk("TSC overhead (Min: %llu, Max: %llu)\n", min_overhead, max_overhead);
}

/* Convenience wrapper called when a core's timer interrupt goes off.  Not to be
 * confused with global timers (like the PIC).  Do not put your code here.  If
 * you want something to happen in the future, set an alarm. */
void timer_interrupt(struct hw_trapframe *hw_tf, void *data)
{
	__trigger_tchain(&per_cpu_info[core_id()].tchain, hw_tf);
}

/* We can overflow/wraparound when we multiply up, but we have to divide last,
 * or else we lose precision.  If we're too big and will overflow, we'll
 * sacrifice precision for correctness, and degrade to the next lower level
 * (losing 3 digits worth).  The recursive case shouldn't overflow, since it
 * called something that scaled down the tsc_time by more than 1000. */
uint64_t tsc2sec(uint64_t tsc_time)
{
	return tsc_time / __proc_global_info.tsc_freq;
}

uint64_t tsc2msec(uint64_t tsc_time)
{
	if (mult_will_overflow_u64(tsc_time, 1000))
		return tsc2sec(tsc_time) * 1000;
	else
		return (tsc_time * 1000) / __proc_global_info.tsc_freq;
}

uint64_t tsc2usec(uint64_t tsc_time)
{
	if (mult_will_overflow_u64(tsc_time, 1000000))
		return tsc2msec(tsc_time) * 1000;
	else
		return (tsc_time * 1000000) / __proc_global_info.tsc_freq;
}

uint64_t tsc2nsec(uint64_t tsc_time)
{
	if (mult_will_overflow_u64(tsc_time, 1000000000))
		return tsc2usec(tsc_time) * 1000;
	else
		return (tsc_time * 1000000000) / __proc_global_info.tsc_freq;
}

uint64_t sec2tsc(uint64_t sec)
{
	if (mult_will_overflow_u64(sec, __proc_global_info.tsc_freq)) {
		/* in this case, we simply can't express the number of ticks */
		warn("Wraparound in sec2tsc(), rounding up");
		return (uint64_t)(-1);
	} else {
		return sec * __proc_global_info.tsc_freq;
	}
}

uint64_t msec2tsc(uint64_t msec)
{
	if (mult_will_overflow_u64(msec, __proc_global_info.tsc_freq))
		return sec2tsc(msec / 1000);
	else
		return (msec * __proc_global_info.tsc_freq) / 1000;
}

uint64_t usec2tsc(uint64_t usec)
{
	if (mult_will_overflow_u64(usec, __proc_global_info.tsc_freq))
		return msec2tsc(usec / 1000);
	else
		return (usec * __proc_global_info.tsc_freq) / 1000000;
}

uint64_t nsec2tsc(uint64_t nsec)
{
	if (mult_will_overflow_u64(nsec, __proc_global_info.tsc_freq))
		return usec2tsc(nsec / 1000);
	else
		return (nsec * __proc_global_info.tsc_freq) / 1000000000;
}

/* TODO: figure out what epoch time TSC == 0 is and store that as boot_tsc */
static uint64_t boot_sec = 1242129600; /* nanwan's birthday */

uint64_t epoch_tsc(void)
{
	return read_tsc() + sec2tsc(boot_sec);
}

uint64_t epoch_sec(void)
{
	return tsc2sec(epoch_tsc());
}

uint64_t epoch_msec(void)
{
	return tsc2msec(epoch_tsc());
}

uint64_t epoch_usec(void)
{
	return tsc2usec(epoch_tsc());
}

uint64_t epoch_nsec(void)
{
	return tsc2nsec(epoch_tsc());
}

void tsc2timespec(uint64_t tsc_time, struct timespec *ts)
{
	ts->tv_sec = tsc2sec(tsc_time);
	/* subtract off everything but the remainder */
	tsc_time -= sec2tsc(ts->tv_sec);
	ts->tv_nsec = tsc2nsec(tsc_time);
}
