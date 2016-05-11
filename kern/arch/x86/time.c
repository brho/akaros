/* Copyright (c) 2009 The Regents of the University of California
 * David (Yu) Zhu <yuzhu@cs.berkeley.edu>
 * Barret Rhoden <brho@cs.berkeley.edu>
 *
 * See LICENSE for details. */

#include <arch/x86.h>
#include <arch/arch.h>
#include <arch/pic.h>
#include <arch/apic.h>
#include <time.h>
#include <trap.h>
#include <assert.h>
#include <stdio.h>
#include <ros/procinfo.h>

static uint16_t pit_divisor;
static uint8_t pit_mode;

// timer init calibrates both tsc timer and lapic timer using PIT
void timer_init(void){
	/* some boards have this unmasked early on. */
	pic_mask_irq(0, 0 + PIC1_OFFSET);
	uint64_t tscval[2];
	long timercount[2];
	pit_set_timer(0xffff, TIMER_RATEGEN);
	// assume tsc exist
	tscval[0] = read_tsc();
	udelay_pit(1000000);
	tscval[1] = read_tsc();
	__proc_global_info.tsc_freq = tscval[1] - tscval[0];
	cprintf("TSC Frequency: %llu\n", __proc_global_info.tsc_freq);
	__lapic_set_timer(0xffffffff, IdtLAPIC_TIMER, FALSE,
	                  LAPIC_TIMER_DIVISOR_BITS);
	// Mask the LAPIC Timer, so we never receive this interrupt (minor race)
	mask_lapic_lvt(MSR_LAPIC_LVT_TIMER);
	timercount[0] = apicrget(MSR_LAPIC_CURRENT_COUNT);
	udelay_pit(1000000);
	timercount[1] = apicrget(MSR_LAPIC_CURRENT_COUNT);
	__proc_global_info.bus_freq = (timercount[0] - timercount[1])
	                         * LAPIC_TIMER_DIVISOR_VAL;
	assert(__proc_global_info.bus_freq);
	/* The time base for the timer is derived from the processor's bus clock,
	 * divided by the value specified in the divide configuration register.
	 * Note we mult and div by the divisor, saving the actual freq (even though
	 * we don't use it yet). */
	cprintf("Bus Frequency: %llu\n", __proc_global_info.bus_freq);
}

void pit_set_timer(uint32_t divisor, uint32_t mode)
{
	if (divisor & 0xffff0000)
		warn("Divisor too large!");
	mode = TIMER_SEL0|TIMER_16BIT|mode;
	outb(TIMER_MODE, mode);
	outb(TIMER_CNTR0, divisor & 0xff);
	outb(TIMER_CNTR0, (divisor >> 8) );
	pit_mode = mode;
	pit_divisor = divisor;
	// cprintf("timer mode set to %d, divisor %d\n",mode, divisor);
}

static int getpit()
{
    int high, low;
	// TODO: need a lock to protect access to PIT

    /* Select counter 0 and latch counter value. */
    outb(TIMER_MODE, TIMER_SEL0 | TIMER_LATCH);

    low = inb(TIMER_CNTR0);
    high = inb(TIMER_CNTR0);

    return ((high << 8) | low);
}

// forces cpu to relax for usec miliseconds.  declared in kern/include/time.h
void udelay(uint64_t usec)
{
	#if !defined(__BOCHS__)
	if (__proc_global_info.tsc_freq != 0)
	{
		uint64_t start, end, now;

		start = read_tsc();
        end = start + usec2tsc(usec);
        //cprintf("start %llu, end %llu\n", start, end);
		if (end == 0) cprintf("This is terribly wrong \n");
		do {
            cpu_relax();
            now = read_tsc();
			//cprintf("now %llu\n", now);
		} while (now < end || (now > start && end < start));
        return;

	} else
	#endif
	{
		udelay_pit(usec);
	}
}

void udelay_pit(uint64_t usec)
{
	int64_t delta, prev_tick, tick, ticks_left;

	if (usec <= 0)
		return;

	prev_tick = getpit();
	/*
	 * Calculate ticks as (usec * (i8254_freq / 1e6)) rounded up
	 * without using floating point and without any avoidable overflows.
	 */
	ticks_left = ((usec * PIT_FREQ) + 999999) / 1000000;
	while (ticks_left > 0) {
		tick = getpit();
		delta = prev_tick - tick;
		prev_tick = tick;
		if (delta < 0) {
			// counter looped around during the delta time period
			delta += pit_divisor; // maximum count
			if (delta < 0)
				delta = 0;
		}
		ticks_left -= delta;
	}
}

uint64_t gettimer(void)
{
	return read_tsc();
}

uint64_t getfreq(void)
{
	return __proc_global_info.tsc_freq;
}

void set_core_timer(uint32_t usec, bool periodic)
{
	if (usec)
		lapic_set_timer(usec, periodic);
	else
		lapic_disable_timer();
}
