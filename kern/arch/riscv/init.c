/* See COPYRIGHT for copyright information. */

#include <smp.h>
#include <arch/init.h>
#include <arch/console.h>

void arch_init()
{		
	smp_boot();
	proc_init();
	keyboard_alarm_init();
}
