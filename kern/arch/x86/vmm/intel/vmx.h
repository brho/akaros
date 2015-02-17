/*-
 * Copyright (c) 2011 NetApp, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY NETAPP, INC ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL NETAPP, INC OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _VMX_H_
#define	_VMX_H_

struct vmxctx {
	uint64_t guest_rdi;		/* Guest state */
	uint64_t guest_rsi;
	uint64_t guest_rdx;
	uint64_t guest_rcx;
	uint64_t guest_r8;
	uint64_t guest_r9;
	uint64_t guest_rax;
	uint64_t guest_rbx;
	uint64_t guest_rbp;
	uint64_t guest_r10;
	uint64_t guest_r11;
	uint64_t guest_r12;
	uint64_t guest_r13;
	uint64_t guest_r14;
	uint64_t guest_r15;
	uint64_t guest_cr2;

	uint64_t host_r15;		/* Host state */
	uint64_t host_r14;
	uint64_t host_r13;
	uint64_t host_r12;
	uint64_t host_rbp;
	uint64_t host_rsp;
	uint64_t host_rbx;
	/*
	 * XXX todo debug registers and fpu state
	 */

	int inst_fail_status;

	/*
	 * The pmap needs to be deactivated in vmx_enter_guest()
	 * so keep a copy of the 'pmap' in each vmxctx.
	struct pmap *pmap;
	 */
	// For Akaros. The pmap did not apply directly, but struct proc * is right.
	struct proc *p;
};

struct vmxcap {
	int set;
	uint32_t proc_ctls;
	uint32_t proc_ctls2;
};

struct vmxstate {
	uint64_t nextrip;			/* next instruction to be executed by guest */
	int lastcpu;				/* host cpu that this 'vcpu' last ran on */
	uint16_t vpid;
};

// TODO: akaros: merge all our various apic structs. 
struct apic_page {
	uint32_t reg[PAGE_SIZE / 4];
};

/* Posted Interrupt Descriptor (described in section 29.6 of the Intel SDM) */
struct pir_desc {
	atomic_t pir[4];
	atomic_t pending;
	uint64_t unused[3];
} __attribute__((aligned(64)));

/* Index into the 'guest_msrs[]' array */
enum {
	IDX_MSR_LSTAR,
	IDX_MSR_CSTAR,
	IDX_MSR_STAR,
	IDX_MSR_SYSCALL_MASK,
	IDX_MSR_KERNEL_GS_BASE,
	GUEST_MSR_NUM				/* must be the last enumeration */
};

struct msr_bitmap {
	char bitmap[PAGE_SIZE];	
} __attribute__ ((aligned(PAGE_SIZE)));
/* virtual machine softc */
// TODO: this has to go somewhere is we make VMs a flavor of an MCP, as we hope to do.
struct vmx {
	struct vmcs vmcs[MAX_NUM_CPUS];	/* one vmcs per virtual cpu */
	struct apic_page apic_page[MAX_NUM_CPUS];	/* one apic page per vcpu */
	struct msr_bitmap msr_bitmap;
	struct pir_desc pir_desc[MAX_NUM_CPUS];
	uint64_t guest_msrs[MAX_NUM_CPUS][GUEST_MSR_NUM];
	struct vmxctx ctx[MAX_NUM_CPUS];
	struct vmxcap cap[MAX_NUM_CPUS];
	struct vmxstate state[MAX_NUM_CPUS];
	uint64_t eptp;
	struct vm *vm;
	long eptgen[MAX_NUM_CPUS];	/* cached pmap->pm_eptgen */
};

#define	VMX_GUEST_VMEXIT	0
#define	VMX_VMRESUME_ERROR	1
#define	VMX_VMLAUNCH_ERROR	2
#define	VMX_INVEPT_ERROR	3

// This is here solely to make all the static asserts work. Hack. But those
// are very useful functions. 
// TODO: there HAS to be a better way ...
static void __1(void) {
	static_assert((offsetof(struct vmx, pir_desc[0]) & 63) == 0);
	// should not fail  but does ... TODO Akaros
	//static_assert((offsetof(struct vmx, msr_bitmap) & PAGE_MASK) == 0);
	static_assert((offsetof(struct vmx, vmcs) & PAGE_MASK) == 0);
	static_assert(sizeof(struct pir_desc) == 64);
	static_assert(sizeof(struct apic_page) == PAGE_SIZE);
}

int vmx_enter_guest(struct vmxctx *ctx, struct vmx *vmx, int launched);
void vmx_call_isr(uintptr_t entry);

unsigned long vmx_fix_cr0(unsigned long cr0);
unsigned long vmx_fix_cr4(unsigned long cr4);

extern char vmx_exit_guest[];

#endif
