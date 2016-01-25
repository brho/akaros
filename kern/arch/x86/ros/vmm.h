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
	void					*pir_addr;
	void					*vapic_addr;
	void					*apic_addr;
};
