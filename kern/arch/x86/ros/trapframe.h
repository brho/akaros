#pragma once

#define ROS_INC_ARCH_TRAPFRAME_H

#ifndef ROS_INC_TRAPFRAME_H
#error "Do not include include ros/arch/trapframe.h directly"
#endif

#include <ros/common.h>

#define ROS_ARCH_REFL_ID 0x1234

/* Page faults return the nature of the fault in the bits of the error code: */
#define PF_ERROR_PRESENT 		0x01
#define PF_ERROR_WRITE 			0x02
#define PF_ERROR_USER 			0x04
#define PF_VMR_BACKED 			(1 << 31)

#include <ros/arch/trapframe64.h>

static inline uintptr_t get_hwtf_pc(struct hw_trapframe *hw_tf)
{
	return hw_tf->tf_rip;
}

static inline uintptr_t get_hwtf_fp(struct hw_trapframe *hw_tf)
{
	return hw_tf->tf_rbp;
}

static inline uintptr_t get_hwtf_sp(struct hw_trapframe *hw_tf)
{
	return hw_tf->tf_rsp;
}

static inline uintptr_t get_swtf_pc(struct sw_trapframe *sw_tf)
{
	return sw_tf->tf_rip;
}

static inline uintptr_t get_swtf_fp(struct sw_trapframe *sw_tf)
{
	return sw_tf->tf_rbp;
}

static inline uintptr_t get_swtf_sp(struct sw_trapframe *sw_tf)
{
	return sw_tf->tf_rsp;
}

static inline uintptr_t get_vmtf_pc(struct vm_trapframe *vm_tf)
{
	return vm_tf->tf_rip;
}

static inline uintptr_t get_vmtf_fp(struct vm_trapframe *vm_tf)
{
	return vm_tf->tf_rbp;
}

static inline uintptr_t get_vmtf_sp(struct vm_trapframe *vm_tf)
{
	return vm_tf->tf_rsp;
}

/* FP state and whatever else the kernel won't muck with automatically.  For
 * now, it's the Non-64-bit-mode layout of FP and XMM registers, as used by
 * FXSAVE and FXRSTOR.  Other modes will require a union on a couple entries.
 * See SDM 2a 3-451. */
/* Header for the non-64-bit mode FXSAVE map */
struct fp_header_non_64bit {
	uint16_t			fcw;
	uint16_t			fsw;
	uint8_t				ftw;
	uint8_t				padding0;
	uint16_t			fop;
	uint32_t			fpu_ip;
	uint16_t			cs;
	uint16_t			padding1;
	uint32_t			fpu_dp;
	uint16_t			ds;
	uint16_t			padding2;
	uint32_t			mxcsr;
	uint32_t			mxcsr_mask;
};

/* Header for the 64-bit mode FXSAVE map with promoted operand size */
struct fp_header_64bit_promoted {
	uint16_t			fcw;
	uint16_t			fsw;
	uint8_t				ftw;
	uint8_t				padding0;
	uint16_t			fop;
	uint64_t			fpu_ip;
	uint64_t			fpu_dp;
	uint32_t			mxcsr;
	uint32_t			mxcsr_mask;
};

/* Header for the 64-bit mode FXSAVE map with default operand size */
struct fp_header_64bit_default {
	uint16_t			fcw;
	uint16_t			fsw;
	uint8_t				ftw;
	uint8_t				padding0;
	uint16_t			fop;
	uint32_t			fpu_ip;
	uint16_t			cs;
	uint16_t			padding1;
	uint32_t			fpu_dp;
	uint16_t			ds;
	uint16_t			padding2;
	uint32_t			mxcsr;
	uint32_t			mxcsr_mask;
};

/* Just for storage space, not for real use	*/
typedef struct {
	unsigned int stor[4];
} __128bits;

/*
 *  X86_MAX_XCR0 specifies the maximum set of processor extended state
 *  feature components that Akaros supports saving through the
 *  XSAVE instructions.
 *  This may be a superset of available state components on a given
 *  processor. We CPUID at boot and determine the intersection
 *  of Akaros-supported and processor-supported features, and we
 *  save this value to __proc_global_info.x86_default_xcr0 in arch/x86/init.c.
 *  We guarantee that the set of feature components specified by
 *  X86_MAX_XCR0 will fit in the ancillary_state struct.
 *  If you add to the mask, make sure you also extend ancillary_state!
 */

#define X86_MAX_XCR0 0x2ff

typedef struct ancillary_state {
	/* Legacy region of the XSAVE area */
	union { /* whichever header used depends on the mode */
		struct fp_header_non_64bit		fp_head_n64;
		struct fp_header_64bit_promoted		fp_head_64p;
		struct fp_header_64bit_default		fp_head_64d;
	};
	/* offset 32 bytes */
	/* 128 bits: 80 for the st0, 48 reserved */
	__128bits			st0_mm0;
	__128bits			st1_mm1;
	__128bits			st2_mm2;
	__128bits			st3_mm3;
	__128bits			st4_mm4;
	__128bits			st5_mm5;
	__128bits			st6_mm6;
	__128bits			st7_mm7;
	/* offset 160 bytes */
	__128bits			xmm0;
	__128bits			xmm1;
	__128bits			xmm2;
	__128bits			xmm3;
	__128bits			xmm4;
	__128bits			xmm5;
	__128bits			xmm6;
	__128bits			xmm7;
	/* xmm8-xmm15 are only available in 64-bit-mode */
	__128bits			xmm8;
	__128bits			xmm9;
	__128bits			xmm10;
	__128bits			xmm11;
	__128bits			xmm12;
	__128bits			xmm13;
	__128bits			xmm14;
	__128bits			xmm15;
	/* offset 416 bytes */
	__128bits			reserv0;
	__128bits			reserv1;
	__128bits			reserv2;
	__128bits			reserv3;
	__128bits			reserv4;
	__128bits			reserv5;
	/* offset 512 bytes */

	/*
	 * XSAVE header (64 bytes, starting at offset 512 from
	 * the XSAVE area's base address)
	 */

	// xstate_bv identifies the state components in the XSAVE area
	uint64_t			xstate_bv;
	/*
	 *	xcomp_bv[bit 63] is 1 if the compacted format is used, else 0.
	 *	All bits in xcomp_bv should be 0 if the processor does not
	 *	support the compaction extensions to the XSAVE feature set.
	 */
	uint64_t			xcomp_bv;
	__128bits			reserv6;

	/* offset 576 bytes */
	/*
	 *	Extended region of the XSAVE area
	 *	We currently support an extended region of up to 2112 bytes,
	 *	for a total ancillary_state size of 2688 bytes.
	 *	This supports x86 state components up through the zmm31
	 *	register.  If you need more, please ask!
	 *	See the Intel Architecture Instruction Set Extensions
	 *	Programming Reference page 3-3 for detailed offsets in this
	 *	region.
	 */
	uint8_t				extended_region[2120];

	/* ancillary state  */
} __attribute__((aligned(64))) ancillary_state_t;
