/*
 * Copyright (c) 2009 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 */

#ifdef __SHARC__
#pragma nosharc
#define SINIT(x) x
#endif

#include <arch/mmu.h>
#include <arch/x86.h>
#include <arch/arch.h>
#include <arch/apic.h>
#include <ros/timer.h>
#include <assert.h>
#include <stdio.h>

system_timing_t RO system_timing = {0, 0, 0xffff, 0};

/*
 * Remaps the Programmable Interrupt Controller to use IRQs 32-47
 * http://wiki.osdev.org/PIC
 * Not 100% on this stuff, after looking over 
 * http://bochs.sourceforge.net/techspec/PORTS.LST  The cascading and other 
 * stuff might need to be in one command, and after that all we are doing
 * is toggling masks.
 */
void pic_remap() 
{
	// start initialization
	outb(PIC1_CMD, 0x11);
	outb(PIC2_CMD, 0x11);
	// set new offsets
	outb(PIC1_DATA, PIC1_OFFSET);
	outb(PIC2_DATA, PIC2_OFFSET);
	// set up cascading
	outb(PIC1_DATA, 0x04);
	outb(PIC2_DATA, 0x02);
	// other stuff (put in 8086/88 mode, or whatever)
	outb(PIC1_DATA, 0x01);
	outb(PIC2_DATA, 0x01);
	// set masks, defaulting to all masked for now
	outb(PIC1_DATA, 0xff);
	outb(PIC2_DATA, 0xff);
}

void pic_mask_irq(uint8_t irq)
{
	if (irq > 7)
		outb(PIC2_DATA, inb(PIC2_DATA) | (1 << (irq - 8)));
	else
		outb(PIC1_DATA, inb(PIC1_DATA) | (1 << irq));
}

void pic_unmask_irq(uint8_t irq)
{
	if (irq > 7) {
		outb(PIC2_DATA, inb(PIC2_DATA) & ~(1 << (irq - 8)));
		outb(PIC1_DATA, inb(PIC1_DATA) & 0xfb); // make sure irq2 is unmasked
	} else
		outb(PIC1_DATA, inb(PIC1_DATA) & ~(1 << irq));
}

/*
 * Sets the LAPIC timer to go off after a certain number of ticks.  The primary
 * clock freq is actually the bus clock, which we figure out during timer_init
 * Unmasking is implied.  Ref SDM, 3A, 9.6.4
 */
void __lapic_set_timer(uint32_t ticks, uint8_t vec, bool periodic, uint8_t div)
{
	// clears bottom bit and then set divider
	write_mmreg32(LAPIC_TIMER_DIVIDE, (read_mmreg32(LAPIC_TIMER_DIVIDE) &~0xf) |
	              (div & 0xf));
	// set LVT with interrupt handling information
	write_mmreg32(LAPIC_LVT_TIMER, vec | (periodic << 17));
	write_mmreg32(LAPIC_TIMER_INIT, ticks);
	// For debugging when we expand this
	//cprintf("LAPIC LVT Timer: 0x%08x\n", read_mmreg32(LAPIC_LVT_TIMER));
	//cprintf("LAPIC Init Count: 0x%08x\n", read_mmreg32(LAPIC_TIMER_INIT));
	//cprintf("LAPIC Current Count: 0x%08x\n", read_mmreg32(LAPIC_TIMER_CURRENT));
}

void lapic_set_timer(uint32_t usec, bool periodic)
{
	// divide the bus clock by 128, which is the max.
	uint32_t ticks = (usec * system_timing.bus_freq / 128) / 1000000;
	assert(ticks > 0);
	__lapic_set_timer(ticks, LAPIC_TIMER_DEFAULT_VECTOR, periodic,
	                  LAPIC_TIMER_DEFAULT_DIVISOR);
}

void set_core_timer(uint32_t usec, bool periodic)
{
	if (usec)
		lapic_set_timer(usec, periodic);
	else
		lapic_disable_timer();
}

uint32_t lapic_get_default_id(void)
{
	uint32_t ebx;
	cpuid(1, 0, &ebx, 0, 0);
	// p6 family only uses 4 bits here, and 0xf is reserved for the IOAPIC
	return (ebx & 0xFF000000) >> 24;
}

// timer init calibrates both tsc timer and lapic timer using PIT
void timer_init(void){
	uint64_t tscval[2];
	long timercount[2];
	pit_set_timer(0xffff, TIMER_RATEGEN);
	// assume tsc exist
	tscval[0] = read_tsc();
	udelay_pit(1000000);
	tscval[1] = read_tsc();
	system_timing.tsc_freq = SINIT(tscval[1] - tscval[0]);
	
	cprintf("TSC Frequency: %llu\n", system_timing.tsc_freq);

	__lapic_set_timer(0xffffffff, LAPIC_TIMER_DEFAULT_VECTOR, FALSE,
	                  LAPIC_TIMER_DEFAULT_DIVISOR);
	// Mask the LAPIC Timer, so we never receive this interrupt (minor race)
	mask_lapic_lvt(LAPIC_LVT_TIMER);
	timercount[0] = read_mmreg32(LAPIC_TIMER_CURRENT);
	udelay_pit(1000000);
	timercount[1] = read_mmreg32(LAPIC_TIMER_CURRENT);
	system_timing.bus_freq = SINIT((timercount[0] - timercount[1])*128);
		
	cprintf("Bus Frequency: %llu\n", system_timing.bus_freq);
}

void pit_set_timer(uint32_t divisor, uint32_t mode)
{
	if (divisor & 0xffff0000)
		warn("Divisor too large!");
	mode = TIMER_SEL0|TIMER_16BIT|mode;
	outb(TIMER_MODE, mode); 
	outb(TIMER_CNTR0, divisor & 0xff);
	outb(TIMER_CNTR0, (divisor >> 8) );
	system_timing.pit_mode = SINIT(mode);
	system_timing.pit_divisor = SINIT(divisor);
	// cprintf("timer mode set to %d, divisor %d\n",mode, divisor);
}

static int getpit()
{
    int high, low;
	// TODO: need a lock to protect access to PIT

    /* Select timer0 and latch counter value. */
    outb(TIMER_MODE, TIMER_SEL0 | TIMER_LATCH);
    
    low = inb(TIMER_CNTR0);
    high = inb(TIMER_CNTR0);

    return ((high << 8) | low);
}

// forces cpu to relax for usec miliseconds.  declared in kern/include/timing.h
void udelay(uint64_t usec)
{
	#if !defined(__BOCHS__)
	if (system_timing.tsc_freq != 0)
	{
		uint64_t start, end, now;

		start = read_tsc();
        end = start + (system_timing.tsc_freq * usec) / 1000000;
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
	prev_tick = getpit();
	/*
	 * Calculate (n * (i8254_freq / 1e6)) without using floating point
	 * and without any avoidable overflows.
	 */
	if (usec <= 0)
		ticks_left = 0;
	// some optimization from bsd code
	else if (usec < 256)
		/*
		 * Use fixed point to avoid a slow division by 1000000.
		 * 39099 = 1193182 * 2^15 / 10^6 rounded to nearest.
		 * 2^15 is the first power of 2 that gives exact results
		 * for n between 0 and 256.
		 */
		ticks_left = ((uint64_t)usec * 39099 + (1 << 15) - 1) >> 15;
	else
		// round up the ticks left
		ticks_left = ((uint64_t)usec * (long long)PIT_FREQ+ 999999)
			     / 1000000; 
	while (ticks_left > 0) {
		tick = getpit();
		delta = prev_tick - tick;
		prev_tick = tick;
		if (delta < 0) {
			// counter looped around during the delta time period
			delta += system_timing.pit_divisor; // maximum count 
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
	return system_timing.tsc_freq;
}

