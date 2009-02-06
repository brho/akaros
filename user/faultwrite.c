// buggy program - faults with a write to location zero

#include <inc/lib.h>

void
umain(void)
{
	*(unsigned*)0 = 0;
}

