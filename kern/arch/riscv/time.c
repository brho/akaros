#include <arch/time.h>
#include <ros/common.h>
#include <arch/trap.h>
#include <arch/arch.h>
#include <stdio.h>
#include <assert.h>

system_timing_t system_timing = {0};

void
timer_init(void)
{
	system_timing.tsc_freq = TSC_HZ;
	cprintf("TSC Frequency: %llu\n", system_timing.tsc_freq);
}

void
set_core_timer(uint32_t usec, bool periodic)
{
	// we could implement periodic timers using one-shot timers,
	// but for now we only support one-shot
	assert(!periodic);

	if (usec)
	{
	  uint32_t clocks =  (uint64_t)usec*TSC_HZ/1000000;

	  int8_t irq_state = 0;
	  disable_irqsave(&irq_state);

	  mtpcr(PCR_COUNT, 0);
	  mtpcr(PCR_COMPARE, clocks);
	  mtpcr(PCR_SR, mfpcr(PCR_SR) | (1 << (IRQ_TIMER+SR_IM_SHIFT)));

	  enable_irqsave(&irq_state);
	}
	else
	{
	  mtpcr(PCR_SR, mfpcr(PCR_SR) & ~(1 << (IRQ_TIMER+SR_IM_SHIFT)));
	}
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
