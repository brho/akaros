/* Copyright (c) 2015 Google Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Arch-independent VMM kernel headers */

#pragma once

#include <ros/arch/vmm.h>

/* VMCALL FUNCTION NUMBERS */
#define VMCALL_PRINTC		0x1
#define VMCALL_SMPBOOT		0x2

#define VMM_CTL_GET_EXITS		1
#define VMM_CTL_SET_EXITS		2
#define VMM_CTL_GET_FLAGS		3
#define VMM_CTL_SET_FLAGS		4

#define VMM_CTL_EXIT_HALT		(1 << 0)
#define VMM_CTL_EXIT_PAUSE		(1 << 1)
#define VMM_CTL_EXIT_MWAIT		(1 << 2)
#define VMM_CTL_ALL_EXITS		((1 << 3) - 1)

#define VMM_CTL_FL_KERN_PRINTC		(1 << 0)
#define VMM_CTL_ALL_FLAGS			(VMM_CTL_FL_KERN_PRINTC)
