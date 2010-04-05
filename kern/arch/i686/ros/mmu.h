#ifndef _ROS_INC_ARCH_MMU_H
#define _ROS_INC_ARCH_MMU_H

#ifndef __ASSEMBLER__
typedef unsigned long pte_t;
typedef unsigned long pde_t;
#endif

// All physical memory mapped at this address
#define KERNBASE        0xC0000000

// Use this if needed in annotations
#define IVY_KERNBASE (0xC000U << 16)

#define PTSHIFT 22
#define PTSIZE (1 << PTSHIFT)

#define PGSHIFT 12
#define PGSIZE (1 << PGSHIFT)

#define JPGSIZE PTSIZE


/* Segment descriptor and macros temporarily here.  Remove them when fixing x86
 * TLS vulns (TLSV) */

// Global descriptor numbers
#define GD_NULL   0x00     // NULL descriptor
#define GD_KT     0x08     // kernel text
#define GD_KD     0x10     // kernel data
#define GD_UT     0x18     // user text
#define GD_UD     0x20     // user data
#define GD_TSS    0x28     // Task segment selector
#define GD_LDT    0x30     // local descriptor table

#ifdef __ASSEMBLER__

/*
 * Macros to build GDT entries in assembly.
 */
#define SEG_NULL						\
	.word 0, 0;						\
	.byte 0, 0, 0, 0
#define SEG(type,base,lim)					\
	.word (((lim) >> 12) & 0xffff), ((base) & 0xffff);	\
	.byte (((base) >> 16) & 0xff), (0x90 | (type)),		\
		(0xC0 | (((lim) >> 28) & 0xf)), (((base) >> 24) & 0xff)

#else	// not __ASSEMBLER__

#include <ros/common.h>

// Segment Descriptors
typedef struct Segdesc {
	unsigned sd_lim_15_0 : 16;  // Low bits of segment limit
	unsigned sd_base_15_0 : 16; // Low bits of segment base address
	unsigned sd_base_23_16 : 8; // Middle bits of segment base address
	unsigned sd_type : 4;       // Segment type (see STS_ constants)
	unsigned sd_s : 1;          // 0 = system, 1 = application
	unsigned sd_dpl : 2;        // Descriptor Privilege Level
	unsigned sd_p : 1;          // Present
	unsigned sd_lim_19_16 : 4;  // High bits of segment limit
	unsigned sd_avl : 1;        // Unused (available for software use)
	unsigned sd_rsv1 : 1;       // Reserved
	unsigned sd_db : 1;         // 0 = 16-bit segment, 1 = 32-bit segment
	unsigned sd_g : 1;          // Granularity: limit scaled by 4K when set
	unsigned sd_base_31_24 : 8; // High bits of segment base address
} segdesc_t;
// Null segment
#define SEG_NULL	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }
// Segment that is loadable but faults when used
#define SEG_FAULT	{ 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 1, 0, 0 }
// Normal segment
#define SEG(type, base, lim, dpl) 									\
{ ((lim) >> 12) & 0xffff, (base) & 0xffff, ((base) >> 16) & 0xff,	\
    type, 1, dpl, 1, (unsigned) (lim) >> 28, 0, 0, 1, 1,			\
    (unsigned) (base) >> 24 }
// System segment (LDT)
#define SEG_SYS(type, base, lim, dpl) 									\
{ ((lim) >> 12) & 0xffff, (base) & 0xffff, ((base) >> 16) & 0xff,	\
    type, 0, dpl, 1, (unsigned) (lim) >> 28, 0, 0, 1, 1,			\
    (unsigned) (base) >> 24 }

#define SEG16(type, base, lim, dpl) 								\
{ (lim) & 0xffff, (base) & 0xffff, ((base) >> 16) & 0xff,			\
    type, 1, dpl, 1, (unsigned) (lim) >> 16, 0, 0, 1, 0,			\
    (unsigned) (base) >> 24 }

#define SEG16ROINIT(seg,type,base,lim,dpl) \
	{\
		(seg).sd_lim_15_0 = SINIT((lim) & 0xffff);\
		(seg).sd_base_15_0 = SINIT((base)&0xffff);\
		(seg).sd_base_23_16 = SINIT(((base)>>16)&0xff);\
		(seg).sd_type = SINIT(type);\
		(seg).sd_s = SINIT(1);\
		(seg).sd_dpl = SINIT(dpl);\
		(seg).sd_p = SINIT(1);\
		(seg).sd_lim_19_16 = SINIT((unsigned)(lim)>>16);\
		(seg).sd_avl = SINIT(0);\
		(seg).sd_rsv1 = SINIT(0);\
		(seg).sd_db = SINIT(1);\
		(seg).sd_g = SINIT(0);\
		(seg).sd_base_31_24 = SINIT((unsigned)(base)>> 24);\
	}

#endif /* !__ASSEMBLER__ */

// Application segment type bits
#define STA_X		0x8	    // Executable segment
#define STA_E		0x4	    // Expand down (non-executable segments)
#define STA_C		0x4	    // Conforming code segment (executable only)
#define STA_W		0x2	    // Writeable (non-executable segments)
#define STA_R		0x2	    // Readable (executable segments)
#define STA_A		0x1	    // Accessed

// System segment type bits
#define STS_T16A	0x1	    // Available 16-bit TSS
#define STS_LDT		0x2	    // Local Descriptor Table
#define STS_T16B	0x3	    // Busy 16-bit TSS
#define STS_CG16	0x4	    // 16-bit Call Gate
#define STS_TG		0x5	    // Task Gate / Coum Transmitions
#define STS_IG16	0x6	    // 16-bit Interrupt Gate
#define STS_TG16	0x7	    // 16-bit Trap Gate
#define STS_T32A	0x9	    // Available 32-bit TSS
#define STS_T32B	0xB	    // Busy 32-bit TSS
#define STS_CG32	0xC	    // 32-bit Call Gate
#define STS_IG32	0xE	    // 32-bit Interrupt Gate
#define STS_TG32	0xF	    // 32-bit Trap Gate

#define SEG_COUNT	7 		// Number of segments in the steady state
#define LDT_SIZE	(8192 * sizeof(segdesc_t))

#endif /* !ROS_INC_ARCH_MMU_H */
