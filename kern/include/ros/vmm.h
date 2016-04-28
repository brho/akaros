/* Copyright (c) 2015 Google Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Arch-independent VMM kernel headers */

#pragma once

#include <ros/arch/vmm.h>

#define	VMM_VMCALL_PRINTF	0x1	/* Enable VMCALL output console hack */

#define VMM_ALL_FLAGS	(VMM_VMCALL_PRINTF)
