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
volatile uint32_t timer_ticks = 0;

asm (
".global handle_timer_interrupt		\n\t"
"handle_timer_interrupt:		\n\t"
"	mov	" XSTR(CORE_ID_REG) ",%l3	\n\t"
"	mov	%psr,%l4			\n\t"
"	cmp	%l3,0				\n\t"
"	bne	1f				\n\t"
"	 mov	%l4,%psr			\n\t"
"	sethi	%hi(timer_ticks),%l4		\n\t"
"	ld	[%l4+%lo(timer_ticks)],%l5	\n\t"
"	inc	%l5				\n\t"
"	st	%l5,[%l4+%lo(timer_ticks)]	\n\t"
"1:	jmp	%l1				\n\t"
"	 rett	%l2				\n\t" );

void
timer_init(void)
{	
	system_timing.tsc_freq = TSC_HZ;
	cprintf("TSC Frequency: %llu\n", system_timing.tsc_freq);
}

void
set_timer(uint32_t usec)
{
	uint32_t clocks =  (uint64_t)usec*TSC_HZ/1000000;
	if(clocks & (clocks-1))
	{
		clocks = ROUNDUPPWR2(clocks);
		warn("set_timer: rounding up to %d usec",
		     (uint64_t)clocks*1000000/TSC_HZ);
	}
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
