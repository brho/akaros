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
static void train_timing(void)
{
	uint64_t min_overhead = UINT64_MAX;
	uint64_t max_overhead = 0;
	uint64_t time, diff;
	int8_t irq_state = 0;

	/* Reset this, in case we run it again.  The use of start/stop to
	 * determine the overhead relies on timing_overhead being 0. */
	__proc_global_info.tsc_overhead = 0;
	/* timing might use cpuid, in which case we warm it up to avoid some
	 * extra variance */
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
	printk("TSC overhead (Min: %llu, Max: %llu)\n", min_overhead,
	       max_overhead); }

/* Convenience wrapper called when a core's timer interrupt goes off.  Not to be
 * confused with global timers (like the PIC).  Do not put your code here.  If
 * you want something to happen in the future, set an alarm. */
void timer_interrupt(struct hw_trapframe *hw_tf, void *data)
{
	__trigger_tchain(&per_cpu_info[core_id()].tchain, hw_tf);
}

/*
 * We use scaled integer arithmetic for converting between TSC clock cycles
 * and nanoseconds. In each case we use a fixed shift value of 32 which
 * gives a very high degree of accuracy.
 *
 * The actual scaling calculations rely on being able use the 128 bit
 * product of two unsigned 64 bit numbers as an intermediate result
 * in the calculation. Fortunately, on x86_64 at least, gcc's 128 bit
 * support is sufficiently good that it generates optimal code for this
 * calculation without the need to write any assembler.
 */
static inline uint64_t mult_shift_64(uint64_t a, uint64_t b, uint8_t shift)
{
	return ((unsigned __int128)a * b) >> shift;
}

static uint64_t cycles_to_nsec_mult;
static uint64_t nsec_to_cycles_mult;

#define CYCLES_TO_NSEC_SHIFT	32
#define NSEC_TO_CYCLES_SHIFT	32

static void cycles_to_nsec_init(uint64_t tsc_freq_hz)
{
	cycles_to_nsec_mult = (NSEC_PER_SEC << CYCLES_TO_NSEC_SHIFT) / tsc_freq_hz;
}

static void nsec_to_cycles_init(uint64_t tsc_freq_hz)
{
	uint64_t divisor = NSEC_PER_SEC;

	/*
	 * In the unlikely event that the TSC frequency is greater
	 * than (1 << 32) we have to lose a little precision to
	 * avoid overflow in the calculation of the multiplier.
	 */
	while (tsc_freq_hz >= ((uint64_t)1 << NSEC_TO_CYCLES_SHIFT)) {
		tsc_freq_hz >>= 1;
		divisor >>= 1;
	}
	nsec_to_cycles_mult = (tsc_freq_hz << NSEC_TO_CYCLES_SHIFT) / divisor;
}

uint64_t tsc2nsec(uint64_t tsc_time)
{
	return mult_shift_64(tsc_time, cycles_to_nsec_mult, CYCLES_TO_NSEC_SHIFT);
}

uint64_t nsec2tsc(uint64_t nsec)
{
	return mult_shift_64(nsec, nsec_to_cycles_mult, NSEC_TO_CYCLES_SHIFT);
}

/*
 * Return nanoseconds since the UNIX epoch, 1st January, 1970.
 */
uint64_t epoch_nsec(void)
{
	uint64_t cycles = read_tsc() - __proc_global_info.tsc_cycles_last;

	return __proc_global_info.walltime_ns_last + tsc2nsec(cycles);
}

void time_init(void)
{
	train_timing();

	__proc_global_info.walltime_ns_last = read_persistent_clock();
	__proc_global_info.tsc_cycles_last  = read_tsc();

	cycles_to_nsec_init(__proc_global_info.tsc_freq);
	nsec_to_cycles_init(__proc_global_info.tsc_freq);
}
