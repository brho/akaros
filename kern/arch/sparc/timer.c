#include <arch/timer.h>
#include <ros/common.h>
#include <arch/trap.h>
#include <arch/arch.h>
#include <stdio.h>
#include <assert.h>

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
	uint32_t ticks = timer_ticks;
	uint64_t tsc_ticks;

	while(ticks == timer_ticks) ;

	ticks = timer_ticks;
	tsc_ticks = read_tsc();

	while(ticks == timer_ticks) ;

	system_timing.tsc_freq = (read_tsc() - tsc_ticks)*INTERRUPT_TIMER_HZ;

	cprintf("TSC Frequency: %llu\n", system_timing.tsc_freq);
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
