/*
 * Copyright 2015 Google Inc.
 *
 * This file is part of Akaros.
 *
 * Akaros is free software: you can redistribute it and/or modify
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
 *
 *
 * A few notes:
 * - We only emulate memory operations, so we don't need to worry about decoding
 *   the target addresses or whatnot.  We could, just for sanity reasons, though
 *   the registers (via modrm/sib) are in the guest virtual address space.  We
 *   operate in the guest physical space.  Having the GPA from the fault makes
 *   this all easier.
 * - Just like with fetch_insn(), since we use the GPA, we assume that the
 *   target of our memory access is also contiguous physically.  The guest could
 *   have two virtual pages, one mapped to something that triggers an EPT fault
 *   and the other doesn't.  The upper part of that access will go to the
 *   adjacent physical page (e.g. a virtio region), and not to the actual
 *   destination that the guest had mapped.  Buyer beware.  I'm less concerned
 *   about this than I am with instructions.
 * - To emulate instructions that set rflags, like add and cmp, I just execute
 *   the instruction with inline asm and pop rflags.  Let the hardware do it.
 * - The inline asm often uses %2,%1 and not %1,%2 for the args to e.g. cmp.
 *   The good book is in Intel syntax.  Code AT&T.  It's easier to read the args
 *   as if they are in the book, and just switch the ASM args like that.
 * - add and cmp (80 /0) have a rex, and the Good Book says to add a
 *   sign-extended imm8 to r/m8.  Extended to what?  I skipped that, and treated
 *   it like a regular imm8.  The rex should apply for register selection still.
 */

#include <parlib/stdio.h>
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

enum x86_register {
	X86_RAX = 0,
	X86_RCX = 1,
	X86_RDX = 2,
	X86_RBX = 3,
	X86_RSP = 4,
	X86_RBP = 5,
	X86_RSI = 6,
	X86_RDI = 7,
	X86_R8  = 8,
	X86_R9  = 9,
	X86_R10 = 10,
	X86_R11 = 11,
	X86_R12 = 12,
	X86_R13 = 13,
	X86_R14 = 14,
	X86_R15 = 15,
};

static const char * const reg_names[] = {
	"rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
	"r8",  "r9",  "r10", "r11", "r12", "r13", "r14", "r15"
};

static const char *regname(uint8_t reg)
{
	return reg_names[reg];
}

/* Helper: points to the reg in the VMTF */
static uint64_t *reg_to_vmtf_reg(struct vm_trapframe *vm_tf, int reg)
{
	switch (reg) {
	case 0:
		return &vm_tf->tf_rax;
	case 1:
		return &vm_tf->tf_rcx;
	case 2:
		return &vm_tf->tf_rdx;
	case 3:
		return &vm_tf->tf_rbx;
	case 4:
		return &vm_tf->tf_rsp;
	case 5:
		return &vm_tf->tf_rbp;
	case 6:
		return &vm_tf->tf_rsi;
	case 7:
		return &vm_tf->tf_rdi;
	case 8:
		return &vm_tf->tf_r8;
	case 9:
		return &vm_tf->tf_r9;
	case 10:
		return &vm_tf->tf_r10;
	case 11:
		return &vm_tf->tf_r11;
	case 12:
		return &vm_tf->tf_r12;
	case 13:
		return &vm_tf->tf_r13;
	case 14:
		return &vm_tf->tf_r14;
	case 15:
		return &vm_tf->tf_r15;
	}
	panic("Unknown reg %d\n", reg);
}

struct x86_decode {
	uint8_t prefix_sz;
	uint8_t opcode_sz;
	uint8_t modrm_sib_sz;
	uint8_t imm_sz;
	uint8_t operand_bytes;
	uint8_t address_bytes;
	bool has_modrm;
	bool is_store;
	bool rex_r;
	bool rex_x;
	bool rex_b;
};

/* We decode instructions in response to memory faults, so most every
 * instruction will have modrm. */
#define X86_DECODE_64_DEFAULT { \
	.prefix_sz = 0, \
	.opcode_sz = 1, \
	.modrm_sib_sz = 0, \
	.imm_sz = 0, \
	.operand_bytes = 4, \
	.address_bytes = 8, \
	.has_modrm = true, \
	.is_store = false, \
	.rex_r = false, \
	.rex_x = false, \
	.rex_b = false, \
}

static void print_decoded_insn(uint8_t *insn, struct x86_decode *d);
static void run_decode_hokey_tests(void);

static int decode_prefix(uint8_t *insn, struct x86_decode *d)
{
	uint8_t *p = insn;

	for (;; p++) {
		/* Operand-size override prefix */
		if (p[0] == 0x66) {
			/* Ignore 0x66 if REX.W changed us to 8 bytes (64).
			 * Though we should only see 0x66 before REX.W.
			 *
			 * If this was handling 32 bit code but with cs.d clear
			 * (default 16), 66 should set us to 4 bytes. */
			if (d->operand_bytes == 4)
				d->operand_bytes = 2;
			continue;
		}
		/* Address-size override prefix */
		if (p[0] == 0x67) {
			d->address_bytes = 4;
			continue;
		}
		/* REX.* */
		if ((p[0] & 0xf0) == 0x40) {
			if (p[0] & 0x08)
				d->operand_bytes = 8;
			if (p[0] & 0x04)
				d->rex_r = true;
			if (p[0] & 0x02)
				d->rex_x = true;
			if (p[0] & 0x01)
				d->rex_b = true;
			continue;
		}
		break;
	}
	d->prefix_sz = p - insn;
	return 0;
}

static uint8_t *get_modrm(uint8_t *insn, struct x86_decode *d)
{
	if (!d->has_modrm)
		return NULL;
	return insn + d->prefix_sz + d->opcode_sz;
}

static uint8_t modrm_sib_bytes_16(int mod, int rm, struct x86_decode *d)
{
	uint8_t ret = 1;	/* counting the mod/rm byte itself */

	switch (mod) {
	case 0:
		if (rm == 6)
			ret += 2; /* disp16 */
		break;
	case 1:
		ret += 1; /* disp8 */
		if (rm == 4)
			ret += 1; /* SIB */
		break;
	case 2:
		ret += 2; /* disp16 */
		break;
	case 3:
		break;
	}
	return ret;
}

static uint8_t modrm_sib_bytes_32(int mod, int rm, struct x86_decode *d)
{
	uint8_t ret = 1;	/* counting the mod/rm byte itself */

	switch (mod) {
	case 0:
		if (rm == 4)
			ret += 1; /* SIB */
		else if (rm == 5)
			ret += 4; /* disp32 */
		break;
	case 1:
		ret += 1; /* disp8 */
		if (rm == 4)
			ret += 1; /* SIB */
		break;
	case 2:
		ret += 4; /* disp32 */
		if (rm == 4) /* SIB */
			ret += 1;
		break;
	case 3:
		break;
	}
	return ret;
}

/* Returns the number of bytes in the instruction due to things encoded by
 * mod/rm, such as displacements (disp32) or the SIB byte, including the mod/rm
 * byte itself. */
static uint8_t modrm_sib_bytes(uint8_t *insn, struct x86_decode *d)
{
	uint8_t *modrm = get_modrm(insn, d);
	int mod, rm;

	if (!modrm)
		return 0;
	mod = *modrm >> 6;
	rm = *modrm & 0x7;

	switch (d->address_bytes) {
	case 2:
		/* We should never see this, but was easy enough to code */
		fprintf(stderr, "decode: %s had %u address bytes!\n", __func__,
			d->address_bytes);
		return modrm_sib_bytes_16(mod, rm, d);
	case 4:
	case 8:
		return modrm_sib_bytes_32(mod, rm, d);
	default:
		panic("decode: %s had %u address bytes!\n", __func__,
			d->address_bytes);
		return 0;
	}
}

static uint8_t modrm_get_reg_16(uint8_t reg, struct x86_decode *d)
{
	return reg;
}

static uint8_t modrm_get_reg_32(uint8_t reg, struct x86_decode *d)
{
	return reg + (d->rex_r ? 8 : 0);
}

static uint64_t get_imm(uint8_t *insn, struct x86_decode *d)
{
	uint8_t *imm = insn + d->prefix_sz + d->opcode_sz + d->modrm_sib_sz;
	uint64_t ret = 0;

	switch (d->imm_sz) {
	case 8:
		ret |= (uint64_t)imm[7] << (8 * 7);
		ret |= (uint64_t)imm[6] << (8 * 6);
		ret |= (uint64_t)imm[5] << (8 * 5);
		ret |= (uint64_t)imm[4] << (8 * 4);
		/* fall-through */
	case 4:
		ret |= (uint64_t)imm[3] << (8 * 3);
		ret |= (uint64_t)imm[2] << (8 * 2);
		/* fall-through */
	case 2:
		ret |= (uint64_t)imm[1] << (8 * 1);
		/* fall-through */
	case 1:
		ret |= (uint64_t)imm[0] << (8 * 0);
		break;
	default:
		panic("Bad IMM size %u", d->imm_sz);
	}
	return ret;
}

/* This is the three-bit (or more with REX) register used by opcodes that have
 * /r.  The first argument for opcodes is in the modrm part, e.g. [eax]+disp8.
 * We don't need to parse that, since we know the faulting GPA. */
static uint8_t modrm_get_reg(uint8_t *insn, struct x86_decode *d)
{
	uint8_t *modrm = get_modrm(insn, d);
	uint8_t reg;

	if (!modrm) {
		fprintf(stderr, "%s called with no modrm!\n, insn: %p\n",
			__func__, insn);
		hexdump(stderr, insn, 15);
		panic("Continuing could corrupt registers");
	}
	reg = (*modrm >> 3) & 7;

	switch (d->address_bytes) {
	case 2:
		fprintf(stderr, "decode: %s had %u address bytes!\n", __func__,
			d->address_bytes);
		return modrm_get_reg_16(reg, d);
	case 4:
	case 8:
		return modrm_get_reg_32(reg, d);
	default:
		panic("decode: %s had %u address bytes!\n", __func__,
			d->address_bytes);
	}
}

/* Decodes the actual opcode, storing things we care about in d.
 * -1 on error (for opcodes we haven't coded up), 0 success.
 *
 * Sets operand_bytes, various sizes, is_store, etc. */
static int decode_opcode(uint8_t *insn, struct x86_decode *d)
{
	uint8_t *opcode = insn + d->prefix_sz;

	/* If we don't set anything, we're using the defaults:  1 byte opcode,
	 * has_modrm, operand_bytes determined by the prefix/64-bit mode, etc */
	switch (opcode[0]) {
	case 0x80:
		switch (modrm_get_reg(insn, d)) {
		case 0: // add
		case 7: // cmp
			break;
		default:
			goto unknown;
		}
		d->imm_sz = 1;
		d->operand_bytes = 1;
		break;
	case 0x81:
		switch (modrm_get_reg(insn, d)) {
		case 0: // add
		case 7: // cmp
			break;
		default:
			goto unknown;
		}
		d->imm_sz = d->address_bytes == 2 ? 2 : 4;
		break;
	case 0x3a:	// cmp /r
		d->operand_bytes = 1;
		break;
	case 0x88:	// mov
	case 0x8a:	// mov
		d->operand_bytes = 1;
		/* Instructions that could be loads or stores differ in bit 2.
		 * e.g.  88 (store, bit 2 unset) vs 8a (load, bit 2 set). */
		d->is_store = !(opcode[0] & 2);
		break;
	case 0x89:	// mov
	case 0x8b:	// mov
		d->is_store = !(opcode[0] & 2);
		break;
	case 0x0f:
		d->opcode_sz = 2;
		switch (opcode[1]) {
		case 0xb7:	// movzw
			d->operand_bytes = 2;
			break;
		case 0xb6:	// movzb
			d->operand_bytes = 1;
			break;
		default:
			goto unknown;
		}
		break;
	default:
	unknown:
		fprintf(stderr, "unknown decode %02x %02x %02x@ %p\n",
			opcode[0], opcode[1], opcode[2], opcode);
		return -1;
	}

	d->modrm_sib_sz = modrm_sib_bytes(insn, d);
	return 0;
}

static void set_rflags_status_bits(uint64_t *rfl, uint64_t new)
{
	*rfl &= ~FL_STATUS;
	new &= FL_STATUS;
	*rfl |= new;
}

static int add_8081(struct guest_thread *gth, uint8_t *insn,
		    struct x86_decode *d, emu_mem_access access,
		    uint64_t gpa)
{
	struct vm_trapframe *vm_tf = gth_to_vmtf(gth);
	int ret;
	uint64_t scratch = 0;
	int8_t imm8, scr8;
	int16_t imm16, scr16;
	int32_t imm32, scr32;
	int64_t imm64, scr64;
	unsigned long rflags;

	ret = access(gth, gpa, &scratch, d->operand_bytes, false);
	if (ret < 0)
		return ret;
	switch (d->operand_bytes) {
	case 1:
		imm8 = get_imm(insn, d);
		/* scratch is input and output, but you can't cast it in an
		 * output operand */
		scr8 = scratch;
		asm volatile ("addb %2,%1; pushfq; popq %0"
			      : "=r"(rflags),
			        "+r"(scr8)
			      : "r"(imm8)
			      : "cc");
		scratch = scr8;
		break;
	case 2:
		imm16 = get_imm(insn, d);
		scr16 = scratch;
		asm volatile ("addw %2,%1; pushfq; popq %0"
			      : "=r"(rflags),
			        "+r"(scr16)
			      : "r"(imm16)
			      : "cc");
		scratch = scr16;
		break;
	case 4:
		imm32 = get_imm(insn, d);
		scr32 = scratch;
		asm volatile ("addl %2,%1; pushfq; popq %0"
			      : "=r"(rflags),
			        "+r"(scr32)
			      : "r"(imm32)
			      : "cc");
		scratch = scr32;
		break;
	case 8:
		imm32 = get_imm(insn, d);
		scr64 = scratch;
		asm volatile ("addq %2,%1; pushfq; popq %0"
			      : "=r"(rflags),
			        "+r"(scr64)
			      : "r"((int64_t)imm32)
			      : "cc");
		scratch = scr64;
		break;
	}
	set_rflags_status_bits(&vm_tf->tf_rflags, rflags);
	return access(gth, gpa, &scratch, d->operand_bytes, true);
}

static int cmp_8081(struct guest_thread *gth, uint8_t *insn,
		    struct x86_decode *d, emu_mem_access access,
		    uint64_t gpa)
{
	struct vm_trapframe *vm_tf = gth_to_vmtf(gth);
	int ret;
	uint64_t scratch = 0;
	int8_t imm8;
	int16_t imm16;
	int32_t imm32;
	int64_t imm64;
	unsigned long rflags;

	ret = access(gth, gpa, &scratch, d->operand_bytes, false);
	if (ret < 0)
		return ret;
	switch (d->operand_bytes) {
	case 1:
		imm8 = get_imm(insn, d);
		asm volatile ("cmpb %2,%1; pushfq; popq %0"
			      : "=r"(rflags)
			      : "r"((int8_t)scratch),
			        "r"(imm8)
			      : "cc");
		break;
	case 2:
		imm16 = get_imm(insn, d);
		asm volatile ("cmpw %2,%1; pushfq; popq %0"
			      : "=r"(rflags)
			      : "r"((int16_t)scratch),
			        "r"(imm16)
			      : "cc");
		break;
	case 4:
		imm32 = get_imm(insn, d);
		asm volatile ("cmpl %2,%1; pushfq; popq %0"
			      : "=r"(rflags)
			      : "r"((int32_t)scratch),
			        "r"(imm32)
			      : "cc");
		break;
	case 8:
		imm32 = get_imm(insn, d);
		asm volatile ("cmpq %2,%1; pushfq; popq %0"
			      : "=r"(rflags)
			      : "r"((int64_t)scratch),
			        "r"((int64_t)imm32)
			      : "cc");
		break;
	}
	set_rflags_status_bits(&vm_tf->tf_rflags, rflags);
	return 0;
}

static int cmp_3a(struct guest_thread *gth, uint8_t *insn,
		  struct x86_decode *d, emu_mem_access access,
		  uint64_t gpa)
{
	struct vm_trapframe *vm_tf = gth_to_vmtf(gth);
	int ret;
	uint64_t scratch = 0;
	int8_t reg8;
	unsigned long rflags;
	int mod_reg = modrm_get_reg(insn, d);

	assert(d->operand_bytes == 1);
	ret = access(gth, gpa, &scratch, 1, false);
	if (ret < 0)
		return ret;
	reg8 = (int8_t)*reg_to_vmtf_reg(vm_tf, mod_reg);
	asm volatile ("cmpb %2,%1; pushfq; popq %0"
		      : "=r"(rflags)
		      : "r"(reg8),
		        "r"((int8_t)scratch)
		      : "cc");
	set_rflags_status_bits(&vm_tf->tf_rflags, rflags);
	return 0;
}

static int execute_op(struct guest_thread *gth, uint8_t *insn,
		      struct x86_decode *d, emu_mem_access access,
		      uint64_t gpa)
{
	struct vm_trapframe *vm_tf = gth_to_vmtf(gth);
	uint8_t *opcode = insn + d->prefix_sz;
	int ret, mod_reg;

	switch (opcode[0]) {
	case 0x80:
	case 0x81:
		switch (modrm_get_reg(insn, d)) {
		case 0: // add
			return add_8081(gth, insn, d, access, gpa);
		case 7: // cmp
			return cmp_8081(gth, insn, d, access, gpa);
		}
		goto unknown;
	case 0x3a:	// cmp
		return cmp_3a(gth, insn, d, access, gpa);
	case 0x89:	// mov
	case 0x8b:	// mov
	case 0x8a:	// mov
	case 0x88:	// mov
		mod_reg = modrm_get_reg(insn, d);
		ret = access(gth, gpa, reg_to_vmtf_reg(vm_tf, mod_reg),
			     d->operand_bytes, d->is_store);
		/* We set a register's value.  For 32-bit operands, we need to
		 * zero the upper 32 bits. */
		if (!ret && !d->is_store && d->operand_bytes == 4)
			*reg_to_vmtf_reg(vm_tf, mod_reg) &= 0xffffffff;
		return ret;
	case 0x0f:
		switch (opcode[1]) {
		case 0xb7:	// movzw
		case 0xb6:	// movzb
			mod_reg = modrm_get_reg(insn, d);
			*reg_to_vmtf_reg(vm_tf, mod_reg) = 0;
			return access(gth, gpa, reg_to_vmtf_reg(vm_tf, mod_reg),
				      d->operand_bytes, d->is_store);
		}
		goto unknown;
	default:
	unknown:
		fprintf(stderr, "unknown execute %02x %02x %02x@ %p\n",
			opcode[0], opcode[1], opcode[2], opcode);
		return -1;
	}
}

static int decode_inst_size(uint8_t *insn, struct x86_decode *d)
{
	return d->prefix_sz + d->opcode_sz + d->modrm_sib_sz +
	       + d->imm_sz;
}

/* Emulates a memory operation that faulted/vmexited.  Despite the file name,
 * this is x86-specific, so we only have at most one address involved.  We have
 * at least one address involved, since it is a memory operation.
 *
 * The main thing our caller provides is a function pointer for accessing
 * memory.  The address is gpa, the register (which doesn't have to be a real
 * register in a VMTF) involved for the source/destination (based on 'store').
 */
int emulate_mem_insn(struct guest_thread *gth, uint8_t *insn,
		     emu_mem_access access, int *advance)
{
	struct x86_decode d[1] = {X86_DECODE_64_DEFAULT};
	uintptr_t gpa;

	// Duh, which way did he go George? Which way did he go?
	// First hit on Google gets you there!
	// This is the guest physical address of the access.
	// This is nice, because if we ever go with more complete
	// instruction decode, knowing this gpa reduces our work:
	// we don't have to find the source address in registers,
	// only the register holding or receiving the value.
	gpa = gth_to_vmtf(gth)->tf_guest_pa;
	if (decode_prefix(insn, d) < 0)
		return -1;
	if (decode_opcode(insn, d) < 0)
		return -1;
	if (execute_op(gth, insn, d, access, gpa) < 0)
		return -1;
	*advance = decode_inst_size(insn, d);

	if (debug_decode) {
		/* This is racy - multiple decoded threads on different cores
		 * will clutter the output. */
		fprintf(stderr, "gpa %p", gpa);
		print_decoded_insn(insn, d);
	}
	return 0;
}

/* Debugging */

static void print_decoded_insn(uint8_t *insn, struct x86_decode *d)
{
	uint8_t inst_sz = decode_inst_size(insn, d);

	fprintf(stderr,
		"oprnd_bs %d, addr_bs %d, reg %2d, imm 0x%08llx, inst_size %2d:",
		d->operand_bytes, d->address_bytes,
		get_modrm(insn, d) ? modrm_get_reg(insn, d) : -1,
		d->imm_sz ? get_imm(insn, d) : 0xdeadbeef, inst_sz);
	for (int i = 0; i < inst_sz; i++)
		fprintf(stderr, " %02x", insn[i]);
	fprintf(stderr, "\n");
}

static int tst_mem_access(struct guest_thread *gth, uintptr_t gpa,
			  unsigned long *regp, size_t size, bool store)
{
	if (store) {
		switch (size) {
		case 1:
			*(uint8_t*)gpa = *(uint8_t*)regp;
			break;
		case 2:
			*(uint16_t*)gpa = *(uint16_t*)regp;
			break;
		case 4:
			*(uint32_t*)gpa = *(uint32_t*)regp;
			break;
		case 8:
			*(uint64_t*)gpa = *(uint64_t*)regp;
			break;
		}
	} else {
		switch (size) {
		case 1:
			*(uint8_t*)regp = *(uint8_t*)gpa;
			break;
		case 2:
			*(uint16_t*)regp = *(uint16_t*)gpa;
			break;
		case 4:
			*(uint32_t*)regp = *(uint32_t*)gpa;
			break;
		case 8:
			*(uint64_t*)regp = *(uint64_t*)gpa;
			break;
		}
	}
	return 0;
}

/* Far from exhaustive, and you need to call it manually.  It actually caught a
 * bug though. */
static void run_decode_hokey_tests(void)
{
	struct guest_thread gth[1] = {0};
	struct vm_trapframe *vm_tf = gth_to_vmtf(gth);
	uint8_t insn[VMM_MAX_INSN_SZ];
	uint8_t ram[16];
	int inst_size, ret;

	vm_tf->tf_guest_pa = (uint64_t)ram;

	// movw [rax],ecx (rax ignored, we use the GPA)
	memcpy(insn, "\x89\x08", 2);
	memset(ram, 0, sizeof(ram));
	vm_tf->tf_rcx = 0x1234;
	ret = emulate_mem_insn(gth, insn, tst_mem_access, &inst_size);
	assert(!ret);
	assert(inst_size = 2);
	assert(*(uint16_t*)ram == 0x1234);

	// add8 /0 [rbx], -1
	memcpy(insn, "\x80\x03\xff", 3);
	memset(ram, 0, sizeof(ram));
	ram[0] = 1;
	ret = emulate_mem_insn(gth, insn, tst_mem_access, &inst_size);
	assert(!ret);
	assert(inst_size == 3);
	assert(ram[0] == 0);
	assert(vm_tf->tf_rflags & FL_ZF);
	assert(!(vm_tf->tf_rflags & FL_SF));

	// REX.W add /0, r/m64/imm32: 84: SIB+disp32 /0. (-1 sign extend)
	memcpy(insn, "\x48\x81\x84\x00\x00\x00\x00\x00\xff\xff\xff\xff", 12);
	memset(ram, 0, sizeof(ram));
	ram[0] = 2;
	ret = emulate_mem_insn(gth, insn, tst_mem_access, &inst_size);
	assert(!ret);
	assert(inst_size == 12);
	assert(*(uint64_t*)ram == 1);
	assert(!(vm_tf->tf_rflags & FL_ZF));
	assert(!(vm_tf->tf_rflags & FL_SF));

	// REX.R movzw, 14: SIB, reg rdx -> r10
	memcpy(insn, "\x44\x0f\xb7\x14\x00", 5);
	memset(ram, 0, sizeof(ram));
	ram[0] = 0x12;
	vm_tf->tf_r10 = 0xffffffffffffffff;
	ret = emulate_mem_insn(gth, insn, tst_mem_access, &inst_size);
	assert(!ret);
	assert(inst_size == 5);
	assert(vm_tf->tf_r10 == 0x12);

	fprintf(stderr, "Hokey decode tests passed\n");
	return;

	/* Helpful debuggers for the debugger */
	fprintf(stderr, "ram %x %x %x %x %x %x %x %x\n",
		ram[0], ram[1], ram[2], ram[3], ram[4], ram[5], ram[6], ram[7]);
}
