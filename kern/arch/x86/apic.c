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
#include <time.h>
#include <assert.h>
#include <stdio.h>
#include <bitmask.h>
#include <arch/coreid.h>

system_timing_t RO system_timing = {0, 0, 0xffff, 0};
bool core_id_ready = FALSE;
spinlock_t piclock = SPINLOCK_INITIALIZER_IRQSAVE;

/* * Remaps the Programmable Interrupt Controller to use IRQs 32-47
 * http://wiki.osdev.org/PIC
 * Check osdev for a more thorough explanation/implementation.
 * http://bochs.sourceforge.net/techspec/PORTS.LST  */
void pic_remap(void)
{
	spin_lock_irqsave(&piclock);
	/* start initialization (ICW1) */
	outb(PIC1_CMD, 0x11);
	outb(PIC2_CMD, 0x11);
	/* set new offsets (ICW2) */
	outb(PIC1_DATA, PIC1_OFFSET);
	outb(PIC2_DATA, PIC2_OFFSET);
	/* set up cascading (ICW3) */
	outb(PIC1_DATA, 0x04);
	outb(PIC2_DATA, 0x02);
	/* other stuff (put in 8086/88 mode, or whatever) (ICW4) */
	outb(PIC1_DATA, 0x01);
	outb(PIC2_DATA, 0x01);
	/* Init done, further data R/W access the interrupt mask */
	/* set masks, defaulting to all masked for now */
	outb(PIC1_DATA, 0xff);
	outb(PIC2_DATA, 0xff);
	spin_unlock_irqsave(&piclock);
}

void pic_mask_irq(int trap_nr)
{
	int irq = trap_nr - PIC1_OFFSET;
	spin_lock_irqsave(&piclock);
	if (irq > 7)
		outb(PIC2_DATA, inb(PIC2_DATA) | (1 << (irq - 8)));
	else
		outb(PIC1_DATA, inb(PIC1_DATA) | (1 << irq));
	spin_unlock_irqsave(&piclock);
}

void pic_unmask_irq(int trap_nr)
{
	int irq = trap_nr - PIC1_OFFSET;
	spin_lock_irqsave(&piclock);
	if (irq > 7) {
		outb(PIC2_DATA, inb(PIC2_DATA) & ~(1 << (irq - 8)));
		outb(PIC1_DATA, inb(PIC1_DATA) & 0xfb); // make sure irq2 is unmasked
	} else
		outb(PIC1_DATA, inb(PIC1_DATA) & ~(1 << irq));
	spin_unlock_irqsave(&piclock);
}

void pic_mask_all(void)
{
	for (int i = 0 + PIC1_OFFSET; i < 16 + PIC1_OFFSET; i++)
		pic_mask_irq(i);
}

/* Aka, the IMR.  Simply reading the data port are OCW1s. */
uint16_t pic_get_mask(void)
{
	uint16_t ret;
	spin_lock_irqsave(&piclock);
	ret = (inb(PIC2_DATA) << 8) | inb(PIC1_DATA);
	spin_unlock_irqsave(&piclock);
	return ret;
}

static uint16_t __pic_get_irq_reg(int ocw3)
{
	uint16_t ret;
	spin_lock_irqsave(&piclock);
	/* OCW3 to PIC CMD to get the register values.  PIC2 is chained, and
	 * represents IRQs 8-15.  PIC1 is IRQs 0-7, with 2 being the chain */
	outb(PIC1_CMD, ocw3);
	outb(PIC2_CMD, ocw3);
	ret = (inb(PIC2_CMD) << 8) | inb(PIC1_CMD);
	spin_unlock_irqsave(&piclock);
	return ret;
}

/* Returns the combined value of the cascaded PICs irq request register */
uint16_t pic_get_irr(void)
{
	return __pic_get_irq_reg(PIC_READ_IRR);
}

/* Returns the combined value of the cascaded PICs irq service register */
uint16_t pic_get_isr(void)
{
	return __pic_get_irq_reg(PIC_READ_ISR);
}

/* Takes a raw vector/trap number (32-47), not a device IRQ (0-15) */
bool pic_check_spurious(int trap_nr)
{
	/* the PIC may send spurious irqs via one of the chips irq 7.  if the isr
	 * doesn't show that irq, then it was spurious, and we don't send an eoi.
	 * Check out http://wiki.osdev.org/8259_PIC#Spurious_IRQs */
	if ((trap_nr == PIC1_SPURIOUS) && !(pic_get_isr() & (1 << 7))) {
		printd("Spurious PIC1 irq!\n");	/* want to know if this happens */
		return TRUE;
	}
	if ((trap_nr == PIC2_SPURIOUS) && !(pic_get_isr() & (1 << 15))) {
		printd("Spurious PIC2 irq!\n");	/* want to know if this happens */
		/* for the cascaded PIC, we *do* need to send an EOI to the master's
		 * cascade irq (2). */
		pic_send_eoi(2 + PIC1_OFFSET);
		return TRUE;
	}
	return FALSE;
}

void pic_send_eoi(int trap_nr)
{
	int irq = trap_nr - PIC1_OFFSET;
	spin_lock_irqsave(&piclock);
	// all irqs beyond the first seven need to be chained to the slave
	if (irq > 7)
		outb(PIC2_CMD, PIC_EOI);
	outb(PIC1_CMD, PIC_EOI);
	spin_unlock_irqsave(&piclock);
}

bool lapic_check_spurious(int trap_nr)
{
	/* FYI: lapic_spurious is 255 on qemu and 15 on the nehalem..  We actually
	 * can set bits 4-7, and P6s have 0-3 hardwired to 0.  YMMV.
	 *
	 * The SDM recommends not using the spurious vector for any other IRQs (LVT
	 * or IOAPIC RTE), since the handlers don't send an EOI.  However, our check
	 * here allows us to use the vector since we can tell the diff btw a
	 * spurious and a real IRQ. */
	uint8_t lapic_spurious = read_mmreg32(LAPIC_SPURIOUS) & 0xff;
	/* Note the lapic's vectors are not shifted by an offset. */
	if ((trap_nr == lapic_spurious) && !lapic_get_isr_bit(lapic_spurious)) {
		/* i'm still curious about these */
		printk("Spurious LAPIC irq %d, core %d!\n", lapic_spurious, core_id());
		lapic_print_isr();
		return TRUE;
	}
	return FALSE;
}

/* Debugging helper.  Note the ISR/IRR are 32 bits at a time, spaced every 16
 * bytes in the LAPIC address space. */
void lapic_print_isr(void)
{
	printk("LAPIC ISR on core %d\n--------------\n", core_id());
	for (int i = 7; i >= 0; i--)
		printk("%3d-%3d: %p\n", (i + 1) * 32 - 1, i * 32,
		       *(uint32_t*)(LAPIC_ISR + i * 0x10));
	printk("LAPIC IRR on core %d\n--------------\n", core_id());
	for (int i = 7; i >= 0; i--)
		printk("%3d-%3d: %p\n", (i + 1) * 32 - 1, i * 32,
		       *(uint32_t*)(LAPIC_IRR + i * 0x10));
}

/* Returns TRUE if the bit 'vector' is set in the LAPIC ISR or IRR (whatever you
 * pass in.  These registers consist of 8, 32 byte registers spaced every 16
 * bytes from the base in the LAPIC. */
static bool __lapic_get_isrr_bit(unsigned long base, uint8_t vector)
{
	int which_reg = vector >> 5;	/* 32 bits per reg */
	uint32_t *lapic_reg = (uint32_t*)(base + which_reg * 0x10);	/* offset 16 */
	return (*lapic_reg & (1 << (vector % 32)) ? 1 : 0);
}

bool lapic_get_isr_bit(uint8_t vector)
{
	return __lapic_get_isrr_bit(LAPIC_ISR, vector);
}

bool lapic_get_irr_bit(uint8_t vector)
{
	return __lapic_get_isrr_bit(LAPIC_IRR, vector);
}

/* This works for any interrupt that goes through the LAPIC, but not things like
 * ExtInts.  To prevent abuse, we'll use it just for IPIs for now (which only
 * come via the APIC).
 *
 * We only check the ISR, due to how we send EOIs.  Since we don't send til
 * after handlers return, the ISR will show pending for the current IRQ.  It is
 * the EOI that clears the bit from the ISR. */
bool ipi_is_pending(uint8_t vector)
{
	return lapic_get_isr_bit(vector);
}

/*
 * Sets the LAPIC timer to go off after a certain number of ticks.  The primary
 * clock freq is actually the bus clock, which we figure out during timer_init
 * Unmasking is implied.  Ref SDM, 3A, 9.6.4
 */
void __lapic_set_timer(uint32_t ticks, uint8_t vec, bool periodic, uint8_t div)
{
#ifdef CONFIG_LOUSY_LAPIC_TIMER
	/* qemu without kvm seems to delay timer IRQs on occasion, and needs extra
	 * IRQs from any source to get them delivered.  periodic does the trick. */
	periodic = TRUE;
#endif
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
	/* If we overflowed a uint32, send in the max timer possible.  The lapic can
	 * only handle a 32 bit.  We could muck with changing the divisor, but even
	 * then, we might not be able to match 4000 sec (based on the bus speed).
	 * The kernel alarm code can handle spurious timer interrupts, so we just
	 * set the timer for as close as we can get to the desired time. */
	uint64_t ticks64 = (usec * system_timing.bus_freq) / LAPIC_TIMER_DIVISOR_VAL
	                    / 1000000;
	uint32_t ticks32 = ((ticks64 >> 32) ? 0xffffffff : ticks64);
	assert(ticks32 > 0);
	__lapic_set_timer(ticks32, LAPIC_TIMER_DEFAULT_VECTOR, periodic,
	                  LAPIC_TIMER_DIVISOR_BITS);
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
	cpuid(0x1, 0x0, 0, &ebx, 0, 0);
	// p6 family only uses 4 bits here, and 0xf is reserved for the IOAPIC
	return (ebx & 0xFF000000) >> 24;
}

// timer init calibrates both tsc timer and lapic timer using PIT
void timer_init(void){
	/* some boards have this unmasked early on. */
	pic_mask_irq(0 + PIC1_OFFSET);
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
	                  LAPIC_TIMER_DIVISOR_BITS);
	// Mask the LAPIC Timer, so we never receive this interrupt (minor race)
	mask_lapic_lvt(LAPIC_LVT_TIMER);
	timercount[0] = read_mmreg32(LAPIC_TIMER_CURRENT);
	udelay_pit(1000000);
	timercount[1] = read_mmreg32(LAPIC_TIMER_CURRENT);
	system_timing.bus_freq = (timercount[0] - timercount[1])
	                         * LAPIC_TIMER_DIVISOR_VAL;
	/* The time base for the timer is derived from the processor's bus clock,
	 * divided by the value specified in the divide configuration register.
	 * Note we mult and div by the divisor, saving the actual freq (even though
	 * we don't use it yet). */
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
	if (system_timing.tsc_freq != 0)
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
