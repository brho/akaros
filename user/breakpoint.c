// program to cause a breakpoint trap

#include <inc/lib.h>

void
umain(void)
{
	asm volatile("int $3");
}

