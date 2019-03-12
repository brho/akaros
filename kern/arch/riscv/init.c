/* See COPYRIGHT for copyright information. */

#include <arch/console.h>
#include <arch/init.h>
#include <smp.h>

void arch_init(void)
{
	smp_boot();
	proc_init();
}
