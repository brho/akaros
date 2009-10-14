/*
 * Copyright (c) 2009 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Arch specific implementations of lib.h functions.
 */

#include <lib.h>

/* Return the vcoreid, which is set in entry.S right before calling libmain.
 * This should only be used in libmain() and main(), before any code that might
 * use a register.  It just returns eax. */
uint32_t newcore(void)
{
	uint32_t ret;
	asm volatile("mov %%o2,%0" : "=r"(ret));
	return ret;
}

/* This should only be used in libmain(), to reset eax before calling main from
 * vcore0 (which is the only one calling libmain). */
void setvcore0(void)
{
	asm volatile ("mov 0,%o2");
}
