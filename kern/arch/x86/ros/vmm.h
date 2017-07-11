/* Copyright (c) 2015 Google Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * x86 VMM kernel headers */

#pragma once

#include <ros/arch/vmx.h>

/* Initialization data provided by the userspace part of the VMM when setting
 * up a guest physical core (vmx vcpu). */
struct vmm_gpcore_init {
	void					*posted_irq_desc;
	void					*vapic_addr;
	void					*apic_addr;
	void					*user_data;
};

/* Intel VM Trap Injection Fields */
#define VM_TRAP_VALID               (1 << 31)
#define VM_TRAP_ERROR_CODE          (1 << 11)
#define VM_TRAP_HARDWARE            (3 << 8)
/* End Intel VM Trap Injection Fields */
