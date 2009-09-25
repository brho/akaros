/* See COPYRIGHT for copyright information. */

#ifdef __SHARC__
#pragma nosharc
#endif

#include <smp.h>

void arch_init()
{		
	// this returns when all other cores are done and ready to receive IPIs
	smp_boot();
	env_init();
}
