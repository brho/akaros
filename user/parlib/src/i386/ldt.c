/*
 * Copyright (c) 2010 The Regents of the University of California
 * See LICENSE for details.
 */

/** @file
 * @brief x86 LDT init.
 *
 * This function is responsible for correctly setting up the the LDT entry for a
 * given core. This is in support of thread local storage on x86.  x86 expects
 * the memory at %gs:0x0 to hold the address of the top of the TLS.
 *
 * @author Paul Pearce <pearce@eecs.berkeley.edu>
 * @author Barret Rhoden <brho@eecs.berkeley.edu>
 */

#include <ros/common.h>
#include <ros/mman.h>
#include <arch/mmu.h>
#include <parlib.h>
#include <hart.h>
#include <stdlib.h>
#include <assert.h>

void ldt_init(uint32_t core_id) {

	extern void **tls_array;
	extern char core0_tls[];

	static void *core0_tls_top_ptr;
	static bool initialized = 0;
	void **tls_handle;

	/* mmap in only the LDT that we need.  once the kernel supports un-FIXED
	 * mmap(), we can stop hardcoding the location (USTACKBOT - LDTSIZE) */
	if (!initialized++) {
		procdata.ldt = sys_mmap((void*)USTACKBOT - LDT_SIZE,
		                        sizeof(segdesc_t) * hart_max_harts(),
		                        PROT_READ | PROT_WRITE,
		                        MAP_ANONYMOUS | MAP_FIXED | MAP_POPULATE, -1, 0);
		sys_getpid(); // force a kernel crossing to reload the LDT
		if (!procdata.ldt) {
			debug("Unable to mmap() an LDT!  Exiting...\n");
			exit(-EFAIL);
		}
	}

	core0_tls_top_ptr = core0_tls + PARLIB_TLS_SIZE;

	// Get a handle to this core's tls
	if (core_id == 0) {
		tls_handle = &core0_tls_top_ptr;	
	} else {
		tls_handle = &tls_array[core_id];
	}

	// Build the segment
	segdesc_t tmp = SEG(STA_W, (uint32_t)tls_handle, (uint32_t)tls_handle + 4, 3);

	// Setup the correct LDT entry for this hart
	procdata.ldt[core_id] = tmp;

	// Create the GS register.
	uint32_t gs = (core_id << 3) | 0x07;

	// Set the GS register.
	asm volatile("movl %0,%%gs" : : "r" (gs));

	// Profit!
}
