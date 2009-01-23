/* See COPYRIGHT for copyright information. */

/* Support for two time-related hardware gadgets: 1) the run time
 * clock with its NVRAM access functions; 2) the 8253 timer, which
 * generates interrupts on IRQ 0.
 */

#include <inc/x86.h>

#include <kern/kclock.h>


unsigned
mc146818_read(unsigned reg)
{
	outb(IO_RTC, reg);
	return inb(IO_RTC+1);
}

void
mc146818_write(unsigned reg, unsigned datum)
{
	outb(IO_RTC, reg);
	outb(IO_RTC+1, datum);
}



