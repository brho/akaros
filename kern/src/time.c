#include <arch/arch.h>
#include <time.h>
#include <stdio.h>
#include <schedule.h>
#include <multiboot.h>
#include <pmap.h>
#include <smp.h>

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
	system_timing.timing_overhead = 0;
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
	system_timing.timing_overhead = min_overhead;
	printk("TSC overhead (Min: %llu, Max: %llu)\n", min_overhead, max_overhead);
}

void udelay_sched(uint64_t usec)
{
	struct timer_chain *tchain = &per_cpu_info[core_id()].tchain;
	struct alarm_waiter a_waiter;
	init_awaiter(&a_waiter, 0);
	set_awaiter_rel(&a_waiter, usec);
	set_alarm(tchain, &a_waiter);
	sleep_on_awaiter(&a_waiter);
}

/* Convenience wrapper called when a core's timer interrupt goes off.  Not to be
 * confused with global timers (like the PIC).  Do not put your code here.  If
 * you want something to happen in the future, set an alarm. */
void timer_interrupt(struct hw_trapframe *hw_tf, void *data)
{
	int coreid = core_id();
	/* run the alarms out of RKM context, so that event delivery works nicely
	 * (keeps the proc lock and ksched lock non-irqsave) */
	/* this is about the only place we can stash this info. */
	per_cpu_info[coreid].rip = hw_tf->tf_rip;
	send_kernel_message(coreid, __trigger_tchain,
	                    (long)&per_cpu_info[coreid].tchain, 0, 0, KMSG_ROUTINE);
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
