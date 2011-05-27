/* See COPYRIGHT for copyright information. */

#include <smp.h>
#include <arch/init.h>

void arch_init()
{		
	smp_boot();
	proc_init();
}
