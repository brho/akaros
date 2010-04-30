/*  Mostly from JOS.   See COPYRIGHT for copyright information. */

#ifndef ROS_INCLUDE_ARCH_TRAPFRAME_H
#define ROS_INCLUDE_ARCH_TRAPFRAME_H

#include <ros/common.h>

typedef struct pushregs {
	/* registers as pushed by pusha */
	uint32_t reg_edi;
	uint32_t reg_esi;
	uint32_t reg_ebp; uint32_t reg_oesp;		/* Useless */
	uint32_t reg_ebx;
	uint32_t reg_edx;
	uint32_t reg_ecx;
	uint32_t reg_eax;
} push_regs_t;

typedef struct trapframe {
	push_regs_t tf_regs;
	uint16_t tf_gs;
	uint16_t tf_padding1;
	uint16_t tf_fs;
	uint16_t tf_padding2;
	uint16_t tf_es;
	uint16_t tf_padding3;
	uint16_t tf_ds;
	uint16_t tf_padding4;
	uint32_t tf_trapno;
	/* below here defined by x86 hardware */
	uint32_t tf_err;
	uintptr_t tf_eip;
	uint16_t tf_cs;
	uint16_t tf_padding5;
	uint32_t tf_eflags;
	/* below here only when crossing rings, such as from user to kernel */
	uintptr_t tf_esp;
	uint16_t tf_ss;
	uint16_t tf_padding6;
} trapframe_t;

/* TODO: consider using a user-space specific trapframe, since they don't need
 * all of this information.  Might do that eventually, but til then: */
#define user_trapframe trapframe

/* FP state and whatever else the kernel won't muck with automatically.  For
 * now, it's the Non-64-bit-mode layout of FP and XMM registers, as used by
 * FXSAVE and FXRSTOR.  Other modes will require a union on a couple entries.
 * See SDM 2a 3-451. */
/* Header for the non-64-bit mode FXSAVE map */
struct fp_header_non_64bit {
	uint16_t		fcw;
	uint16_t		fsw;
	uint8_t			ftw;
	uint8_t			padding0;
	uint16_t		fop;
	uint32_t		fpu_ip;
	uint16_t		cs;
	uint16_t		padding1;
	uint32_t		fpu_dp;
	uint16_t		ds;
	uint16_t		padding2;
	uint32_t		mxcsr;
	uint32_t		mxcsr_mask;
};

/* Header for the 64-bit mode FXSAVE map with promoted operand size */
struct fp_header_64bit_promoted {
	uint16_t		fcw;
	uint16_t		fsw;
	uint8_t			ftw;
	uint8_t			padding0;
	uint16_t		fop;
	uint64_t		fpu_ip;
	uint64_t		fpu_dp;
	uint32_t		mxcsr;
	uint32_t		mxcsr_mask;
};

/* Header for the 64-bit mode FXSAVE map with default operand size */
struct fp_header_64bit_default {
	uint16_t		fcw;
	uint16_t		fsw;
	uint8_t			ftw;
	uint8_t			padding0;
	uint16_t		fop;
	uint32_t		fpu_ip;
	uint16_t		cs;
	uint16_t		padding1;
	uint32_t		fpu_dp;
	uint16_t		ds;
	uint16_t		padding2;
	uint32_t		mxcsr;
	uint32_t		mxcsr_mask;
};

/* Just for storage space, not for real use	*/
typedef struct {
	unsigned int stor[4];
} __uint128_t;

typedef struct ancillary_state {
	union { /* whichever header used depends on the mode */
		struct fp_header_non_64bit			fp_head_n64;
		struct fp_header_64bit_promoted		fp_head_64p;
		struct fp_header_64bit_default		fp_head_64d;
	};
	__uint128_t		st0_mm0;	/* 128 bits: 80 for the st0, 48 rsv */
	__uint128_t		st1_mm1;
	__uint128_t		st2_mm2;
	__uint128_t		st3_mm3;
	__uint128_t		st4_mm4;
	__uint128_t		st5_mm5;
	__uint128_t		st6_mm6;
	__uint128_t		st7_mm7;
	__uint128_t		xmm0;
	__uint128_t		xmm1;
	__uint128_t		xmm2;
	__uint128_t		xmm3;
	__uint128_t		xmm4;
	__uint128_t		xmm5;
	__uint128_t		xmm6;
	__uint128_t		xmm7;
	__uint128_t		xmm8;		/* xmm8 and below only for 64-bit-mode */
	__uint128_t		xmm9;
	__uint128_t		xmm10;
	__uint128_t		xmm11;
	__uint128_t		xmm12;
	__uint128_t		xmm13;
	__uint128_t		xmm14;
	__uint128_t		xmm15;
	__uint128_t		reserv0;
	__uint128_t		reserv1;
	__uint128_t		reserv2;
	__uint128_t		reserv3;
	__uint128_t		reserv4;
	__uint128_t		reserv5;
} __attribute__((aligned(16))) ancillary_state_t;

#endif /* !ROS_INCLUDE_ARCH_TRAPFRAME_H */
