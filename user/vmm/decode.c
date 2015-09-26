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

// Since we at most have to decode less than half of each instruction, I'm trying to be dumb here.
// Fortunately, for me, that's not hard.
// I'm trying to avoid the whole Big Fun of full instruction decode, and in most of these
// cases we only have to know register, address, operation size, and instruction length.
// The ugly messiness of the SIB and all that are not yet needed. Maybe they
// never will be.

// Target size -- 1, 2, 4, or 8 bytes. We have yet to see 64 bytes. 
// TODO: if we ever see it, test the prefix. Since this only supports the low 1M,
// that's not likely.
static int target(void *insn, int *store) 
{
	*store = 0;
	int s = -1;
	uint8_t *byte = insn;
	uint16_t *word = insn;

	if (*byte == 0x66) {
		s = target(insn+1,store);
		// flip the sense of s.
		s = s == 4 ? 2 : 4;
		return s;
	}
	if (*byte == 0x44) {
		byte++;
		word++;
	}
	switch(*byte) {
	case 0x3a:
	case 0x8a:
	case 0x88:
		s = 1;
		break;
	case 0x89:
	case 0x8b:
		s = 2;
		break;
	case 0x81:
		s = 4;	
		break;
	case 0x0f:
	switch(*word) {
		case 0xb70f:
			s = 4;
			break;
		default:
			fprintf(stderr, "can't get size of %02x/%04x @ %p\n", *byte, *word, byte);
			return -1;
			break;
		}
		break;
	default:
		fprintf(stderr, "can't get size of %02x @ %p\n", *byte, byte);
		return -1;
		break;
	}

	switch(*byte) {
	case 0x3a:
	case 0x8a:
	case 0x88:
	case 0x89:
	case 0x8b:
	case 0x81:
		*store = !(*byte & 2);
		break;
	default:
		fprintf(stderr, "%s: Can't happen\n", __func__);
		break;
	}
	return s;
}

char *regname(uint8_t reg)
{
	return modrmreg[reg];
}

static int insize(void *rip)
{
	uint8_t *kva = rip;
	int advance = 3;
	int extra = 0;
	if (kva[0] == 0x44) {
		extra = 1;
		kva++;
	}

	/* the dreaded mod/rm byte. */
	int mod = kva[1]>>6;
	int rm = kva[1] & 7;

	switch(kva[0]) {
	default: 
		fprintf(stderr, "BUG! %s got 0x%x\n", __func__, kva[0]);
	case 0x0f: 
		break;
	case 0x81:
		advance = 6 + extra;
		break;
	case 0x3a:
	case 0x8a:
	case 0x88:
	case 0x89:
	case 0x8b:
		switch (mod) {
		case 0: 
			advance = 2 + (rm == 4) + extra;
			break;
		case 1:
			advance = 3 + (rm == 4) + extra;
			break;
		case 2: 
			advance = 6 + (rm == 4) + extra;
			break;
		case 3:
			advance = 2 + extra;
			break;
		}
		break;
	}
	return advance;
}

// This is a very limited function. It's only here to manage virtio-mmio and low memory
// pointer loads. I am hoping it won't grow with time. The intent is that we enter it with
// and EPT fault from a region that is deliberately left unbacked by any memory. We return
// enough info to let you emulate the operation if you want. Because we have the failing physical
// address (gpa) the decode is far simpler because we only need to find the register, how many bytes
// to move, and how big the instruction is. I thought about bringing in emulate.c from kvm from xen,
// but it has way more stuff than we need.
// gpa is a pointer to the gpa. 
// int is the reg index which we can use for printing info.
// regp points to the register in hw_trapframe from which
// to load or store a result.
int decode(struct vmctl *v, uint64_t *gpa, uint8_t *destreg, uint64_t **regp, int *store, int *size, int *advance)
{

	DPRINTF("v is %p\n", v);

	// Duh, which way did he go George? Which way did he go? 
	// First hit on Google gets you there!
	// This is the guest physical address of the access.
	// This is nice, because if we ever go with more complete
	// instruction decode, knowing this gpa reduces our work:
	// we don't have to find the source address in registers,
	// only the register holding or receiving the value.
	*gpa = v->gpa;
	DPRINTF("gpa is %p\n", *gpa);

	// To find out what to do, we have to look at
	// RIP. Technically, we should read RIP, walk the page tables
	// to find the PA, and read that. But we're in the kernel, so
	// we take a shortcut for now: read the low 30 bits and use
	// that as the kernel PA, or our VA, and see what's
	// there. Hokey. Works.
	uint8_t *kva = (void *)(v->regs.tf_rip & 0x3fffffff);
	DPRINTF("kva is %p\n", kva);

	// fail fast. If we can't get the size we're done.
	*size = target(kva, store);
	if (*size < 0)
		return -1;

	*advance = insize(kva);

	uint16_t ins = *(uint16_t *)(kva + (kva[0] == 0x44));
	DPRINTF("ins is %04x\n", ins);
		
	*destreg = (ins>>11) & 7;
	*destreg += 8*(kva[0] == 0x44);
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
	case 8:
		*regp = &v->regs.tf_r8;
		break;
	case 9:
		*regp = &v->regs.tf_r9;
		break;
	case 10:
		*regp = &v->regs.tf_r10;
		break;
	case 11:
		*regp = &v->regs.tf_r11;
		break;
	case 12:
		*regp = &v->regs.tf_r12;
		break;
	case 13:
		*regp = &v->regs.tf_r13;
		break;
	case 14:
		*regp = &v->regs.tf_r14;
		break;
	case 15:
		*regp = &v->regs.tf_r15;
		break;
	}
	return 0;
}

#if 0
// stupid emulator since what we need is so limited.
int emu(struct vmctl *v, uint64_t gpa, uint8_t destreg, uint64_t *regp, int store, int size, int advance)
{
	uint8_t *kva = f->regs.tf_rip;

	if (
	switch(kva[0]) {

				val = *(uint64_t*) (lowmem + gpa); 
				printf("val %p ", val);
				memcpy(regp, &val, size);

}
#endif
