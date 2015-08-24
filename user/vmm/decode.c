/*
 * Copyright 2015 Google Inc.
 *
 * This file is part of Akaros.
 *
 * Akarosn is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
 * 
 * Akaros is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * Lesser GNU General Public License for more details.
 * 
 * See COPYING.LESSER for details on the GNU Lesser General Public License.
 * See COPYING for details on the GNU General Public License.
 */

#include <stdio.h>
#include <sys/types.h>
#include <pthread.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <parlib/arch/arch.h>
#include <parlib/ros_debug.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/uio.h>
#include <stdint.h>
#include <err.h>
#include <sys/mman.h>
#include <vmm/vmm.h>
#include <vmm/virtio.h>
#include <vmm/virtio_mmio.h>
#include <vmm/virtio_ids.h>
#include <vmm/virtio_config.h>

int debug_decode = 0;
#define DPRINTF(fmt, ...) \
	if (debug_decode) { printf("decode: " fmt , ## __VA_ARGS__); }

static char *modrmreg[] = {"rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi"};

char *regname(uint8_t reg)
{
	return modrmreg[reg];
}

// This is a very limited function. It's only here to manage virtio-mmio and acpi root
// pointer loads. I am hoping it won't grow with time. The intent is that we enter it with
// and EPT fault from a region that is deliberately left unbacked by any memory. We return
// enough info to let you emulate the operation if you want.
// gpa is a pointer to the gpa. 
// int is the reg index which we can use for printing info.
// regp points to the register in hw_trapframe from which
// to load or store a result.
int decode(struct vmctl *v, uint64_t *gpa, uint8_t *destreg, uint64_t **regp, int *store)
{
	int advance = 3; /* how much to move the IP forward at the end. 3 is a good default. */
	//DPRINTF("v is %p\n", v);

	// Duh, which way did he go George? Which way did he go? 
	// First hit on Google gets you there!
	// This is the guest physical address of the access.
	// This is nice, because if we ever go with more complete
	// instruction decode, knowing this gpa reduces our work:
	// we don't have to find the source address in registers,
	// only the register holding or receiving the value.
	*gpa = v->gpa;
	//DPRINTF("gpa is %p\n", *gpa);

	// To find out what to do, we have to look at
	// RIP. Technically, we should read RIP, walk the page tables
	// to find the PA, and read that. But we're in the kernel, so
	// we take a shortcut for now: read the low 30 bits and use
	// that as the kernel PA, or our VA, and see what's
	// there. Hokey. Works.
	uint8_t *kva = (void *)(v->regs.tf_rip & 0x3fffffff);
	//DPRINTF("kva is %p\n", kva);

	// If this gets any longer we'll have to make a smarter loop. I'm betting it
	// won't
	if ((kva[0] != 0x8b) && (kva[0] != 0x89) && (kva[0] != 0x0f || kva[1] != 0xb7)) {
		fprintf(stderr, "%s: can't handle instruction 0x%x\n", kva[0]);
		return -1;
	}

	uint16_t ins = *(uint16_t *)kva;
	//DPRINTF("ins is %04x\n", ins);
	
	*store = (kva[0] == 0x8b) ? 0 : 1;

	if (kva[0] != 0x0f) {
		/* the dreaded mod/rm byte. */
		int mod = kva[1]>>6;
		switch (mod) {
		case 0: 
		case 3:
			advance = 2;
			break;
		case 1:
			advance = 3;
			break;
		case 2: 
			advance = 6;
			break;
		}
	}

	*destreg = (ins>>11) & 7;
	// Our primitive approach wins big here.
	// We don't have to decode the register or the offset used
	// in the computation; that was done by the CPU and is the gpa.
	// All we need to know is which destination or source register it is.
	switch (*destreg) {
	case 0:
		*regp = &v->regs.tf_rax;
		break;
	case 1:
		*regp = &v->regs.tf_rcx;
		break;
	case 2:
		*regp = &v->regs.tf_rdx;
		break;
	case 3:
		*regp = &v->regs.tf_rbx;
		break;
	case 4:
		*regp = &v->regs.tf_rsp; // uh, right.
		break;
	case 5:
		*regp = &v->regs.tf_rbp;
		break;
	case 6:
		*regp = &v->regs.tf_rsi;
		break;
	case 7:
		*regp = &v->regs.tf_rdi;
		break;
	}
	v->regs.tf_rip += advance;
	DPRINTF("Advance rip by %d bytes to %p\n", advance, v->regs.tf_rip);
	return 0;
}

