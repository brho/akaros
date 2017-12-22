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
#include <ros/arch/mmu.h>
#include <ros/arch/trapframe.h>

int debug_decode = 0;
#define DPRINTF(fmt, ...) \
	do { \
		if (debug_decode) { \
			fprintf(stderr, "decode: " fmt, ## __VA_ARGS__); \
		} \
	} \
	while (0)

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
		// TODO: To really know, for sure, that this is 32 bit, we'd likely have
		//       to check the segment descriptor for the guest's current code
		//       segment in it's GDT. The D flag (bit 22) determines whether the
		//       instruction is using 32 or 16-bit operand size. I'm just going
		//       to assume the flag is set (meaning 32 bit operands) for now, in
		//       order to make virtio work. But really we should check if we
		//       want to know for sure. Note that this hack (changing the below
		//       line) only applies to mov instructions.
		//
		//       And I think there's also a prefix you can use to switch the
		//       instruction to 16-bit addressing
		//       (address-size override prefix?)
		s = 4;
		break;
	case 0x81:
		s = 4;
		break;
	case 0x0f:
		switch (*word) {
			case 0xb70f:
				s = 2;
				break;
			default:
				fprintf(stderr, "can't get size of %02x/%04x @ %p\n", *byte,
				        *word, byte);
				return -1;
		}
		break;
	case 0x41:
		/* VEX byte for modrm field */
		switch (*word) {
			case 0x8a41:
				s = 1;
				break;
			default:
				fprintf(stderr, "unparsed vex instruction %02x/%04x @ %p\n",
				        *byte, *word, byte);
				return -1;
		}
		break;
	default:
		fprintf(stderr, "can't get size of %02x @ %p\n", *byte, byte);
		fprintf(stderr, "can't get WORD of %04x @ %p\n", *word, word);
		return -1;
		break;
	}

	switch(*byte) {
	case 0x0f:
	case 0x41:
		break;
	case 0x3a:
	case 0x8a:
	case 0x88:
	case 0x89:
	case 0x8b:
	case 0x81:
		*store = !(*byte & 2);
		break;
	default:
		fprintf(stderr, "%s: Can't happen. rip is: %p\n", __func__, byte);
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
	uint8_t *rip_gpa = rip;
	int advance = 3;
	int extra = 0;
	if (rip_gpa[0] == 0x44) {
		extra = 1;
		rip_gpa++;
	}

	/* return 3 to handle this specific instruction case. We don't want this
	 * to turn into a fully fledged decode.
	 * This specific instruction is an extended move using r9. It uses the
	 * VEX byte to extend the register bits. */
	if (rip_gpa[0] == 0x41 && rip_gpa[1] == 0x8a && rip_gpa[2] == 0x01)
		return 3;
	/* the dreaded mod/rm byte. */
	int mod = rip_gpa[1] >> 6;
	int rm = rip_gpa[1] & 7;

	switch (rip_gpa[0]) {
	default:
		fprintf(stderr, "BUG! %s got 0x%x\n", __func__, rip_gpa[0]);
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
int decode(struct guest_thread *vm_thread, uint64_t *gpa, uint8_t *destreg,
           uint64_t **regp, int *store, int *size, int *advance)
{
	struct vm_trapframe *vm_tf = &(vm_thread->uthread.u_ctx.tf.vm_tf);
	uint8_t *rip_gpa = NULL;

	DPRINTF("v is %p\n", vm_tf);

	// Duh, which way did he go George? Which way did he go?
	// First hit on Google gets you there!
	// This is the guest physical address of the access.
	// This is nice, because if we ever go with more complete
	// instruction decode, knowing this gpa reduces our work:
	// we don't have to find the source address in registers,
	// only the register holding or receiving the value.
	*gpa = vm_tf->tf_guest_pa;
	DPRINTF("gpa is %p\n", *gpa);

	DPRINTF("rip is %p\n", vm_tf->tf_rip);

	if (rippa(vm_thread, (uint64_t *)&rip_gpa))
		return VM_PAGE_FAULT;
	DPRINTF("rip_gpa is %p\n", rip_gpa);

	// fail fast. If we can't get the size we're done.
	*size = target(rip_gpa, store);
	DPRINTF("store is %d\n", *store);
	if (*size < 0)
		return -1;

	*advance = insize(rip_gpa);

	uint16_t ins = *(uint16_t *)(rip_gpa +
	    ((rip_gpa[0] == 0x44) || (rip_gpa[0] == 0x0f) || (rip_gpa[0] == 0x41)));

	DPRINTF("ins is %04x\n", ins);

	*destreg = (ins>>11) & 7;
	*destreg += 8 * (rip_gpa[0] == 0x44);
	// Our primitive approach wins big here.
	// We don't have to decode the register or the offset used
	// in the computation; that was done by the CPU and is the gpa.
	// All we need to know is which destination or source register it is.
	switch (*destreg) {
	case 0:
		*regp = &vm_tf->tf_rax;
		break;
	case 1:
		*regp = &vm_tf->tf_rcx;
		break;
	case 2:
		*regp = &vm_tf->tf_rdx;
		break;
	case 3:
		*regp = &vm_tf->tf_rbx;
		break;
	case 4:
		*regp = &vm_tf->tf_rsp; // uh, right.
		break;
	case 5:
		*regp = &vm_tf->tf_rbp;
		break;
	case 6:
		*regp = &vm_tf->tf_rsi;
		break;
	case 7:
		*regp = &vm_tf->tf_rdi;
		break;
	case 8:
		*regp = &vm_tf->tf_r8;
		break;
	case 9:
		*regp = &vm_tf->tf_r9;
		break;
	case 10:
		*regp = &vm_tf->tf_r10;
		break;
	case 11:
		*regp = &vm_tf->tf_r11;
		break;
	case 12:
		*regp = &vm_tf->tf_r12;
		break;
	case 13:
		*regp = &vm_tf->tf_r13;
		break;
	case 14:
		*regp = &vm_tf->tf_r14;
		break;
	case 15:
		*regp = &vm_tf->tf_r15;
		break;
	}
	/* Handle movz{b,w}X.  Zero the destination. */
	if ((rip_gpa[0] == 0x0f) && (rip_gpa[1] == 0xb6)) {
		/* movzb.
		 * TODO: figure out if the destination size is 16 or 32 bits.  Linux
		 * doesn't call this yet, so it's not urgent. */
		return -1;
	}
	if ((rip_gpa[0] == 0x0f) && (rip_gpa[1] == 0xb7)) {
		/* movzwl.  Destination is 32 bits, unless we had the rex prefix */
		**regp &= ~((1ULL << 32) - 1);
	}
	return 0;
}
