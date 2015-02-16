#ifndef ROS_INC_ARCH_TRAPFRAME_H
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
} __128bits;

typedef struct ancillary_state {
	union { /* whichever header used depends on the mode */
		struct fp_header_non_64bit			fp_head_n64;
		struct fp_header_64bit_promoted		fp_head_64p;
		struct fp_header_64bit_default		fp_head_64d;
	};
	__128bits		st0_mm0;	/* 128 bits: 80 for the st0, 48 rsv */
	__128bits		st1_mm1;
	__128bits		st2_mm2;
	__128bits		st3_mm3;
	__128bits		st4_mm4;
	__128bits		st5_mm5;
	__128bits		st6_mm6;
	__128bits		st7_mm7;
	__128bits		xmm0;
	__128bits		xmm1;
	__128bits		xmm2;
	__128bits		xmm3;
	__128bits		xmm4;
	__128bits		xmm5;
	__128bits		xmm6;
	__128bits		xmm7;
	__128bits		xmm8;		/* xmm8 and below only for 64-bit-mode */
	__128bits		xmm9;
	__128bits		xmm10;
	__128bits		xmm11;
	__128bits		xmm12;
	__128bits		xmm13;
	__128bits		xmm14;
	__128bits		xmm15;
	__128bits		reserv0;
	__128bits		reserv1;
	__128bits		reserv2;
	__128bits		reserv3;
	__128bits		reserv4;
	__128bits		reserv5;
} __attribute__((aligned(16))) ancillary_state_t;

#endif /* ROS_INC_ARCH_TRAPFRAME_H */
