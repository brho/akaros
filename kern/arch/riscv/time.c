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
  mtpcr(PCR_COUNT, 0);
  mtpcr(PCR_COMPARE, 0);
	mtpcr(PCR_SR, mfpcr(PCR_SR) | (SR_IM & (1 << (TIMER_IRQ+SR_IM_SHIFT))));

	system_timing.tsc_freq = TSC_HZ;
	cprintf("TSC Frequency: %llu\n", system_timing.tsc_freq);
}

/* Warning: one-shot timers are unsupported; all timers are periodic.
 * Perhaps this support could be added with a per_cpu boolean, set
 * by set_core_timer, and interpreted by the interrupt handler. */
void
set_core_timer(uint32_t usec, bool periodic)
{
	uint32_t clocks =  (uint64_t)usec*TSC_HZ/1000000;

  int8_t irq_state = 0;
	disable_irqsave(&irq_state);

  mtpcr(PCR_COMPARE, mfpcr(PCR_COUNT) + clocks);

	enable_irqsave(&irq_state);
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
