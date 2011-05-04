#include <arch/timer.h>
#include <ros/common.h>
#include <arch/trap.h>
#include <arch/arch.h>
#include <stdio.h>
#include <assert.h>

#ifdef __SHARC__
#pragma nosharc
#endif

system_timing_t system_timing = {0};

void
timer_init(void)
{	
	system_timing.tsc_freq = TSC_HZ;
	cprintf("TSC Frequency: %llu\n", system_timing.tsc_freq);
}

/* Warning!  Sparc is unable to do a one-shot timer, so all timers are periodic,
 * though that is not normally what we want. */
void
set_core_timer(uint32_t usec, bool periodic)
{
	set_timer(usec);
}

void
set_timer(uint32_t usec)
{
	uint32_t clocks =  (uint64_t)usec*TSC_HZ/1000000;
	if(clocks & (clocks-1))
		clocks = ROUNDUPPWR2(clocks);

	if(clocks > TIMER_MAX_PERIOD)
	{
		clocks = TIMER_MAX_PERIOD;
		warn("set_timer: truncating to %d usec",
		     (uint64_t)clocks*1000000/TSC_HZ);
	}
	sparc_set_timer(clocks,!!clocks);
}

void
udelay(uint64_t usec)
{
	if (system_timing.tsc_freq != 0)
	{
		uint64_t start, end, now;
        
		start = read_tsc();
		end = start + (system_timing.tsc_freq * usec) / 1000000;

		do
		{
			cpu_relax();
			now = read_tsc();
		} while (now < end || (now > start && end < start));
	}
	else panic("udelay() was called before timer_init(), moron!");
}
