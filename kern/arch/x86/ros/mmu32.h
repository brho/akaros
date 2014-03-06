#ifndef ROS_INC_ARCH_MMU32_H
#define ROS_INC_ARCH_MMU32_H

#ifndef ROS_INC_ARCH_MMU_H
#error "Do not include include ros/arch/mmu32.h directly"
#endif

#ifndef __ASSEMBLER__
#include <ros/common.h>
typedef unsigned long pte_t;
typedef unsigned long pde_t;
#endif

/* x86's 32 bit Virtual Memory Map.  Symbols are similar on other archs
 *
 * Virtual memory map:                                Permissions
 *                                                    kernel/user
 *
 *    4 Gig -------->  +------------------------------+
 *                     :              .               :
 *  KERN_VMAP_TOP      +------------------------------+ 0xfec00000
 *                     |                              |
 *                     ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ RW/--
 *                     :              .               :
 *                     :              .               :
 *                     :              .               :
 *                     |~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~| RW/--
 *                     |                              | RW/--
 *                     |   Remapped Physical Memory   | RW/--
 *                     |                              | RW/--
 *    KERNBASE ----->  +------------------------------+ 0xc0000000
 *                     |  Cur. Page Table (Kern. RW)  | RW/--  PTSIZE
 *    VPT          --> +------------------------------+ 0xbfc00000
 *                     |          Local APIC          | RW/--  APIC_SIZE
 *    LAPIC        --> +------------------------------+ 0xbfb00000
 *                     |            IOAPIC            | RW/--  APIC_SIZE
 *    IOAPIC,      --> +------------------------------+ 0xbfa00000
 *  KERN_DYN_TOP       |   Kernel Dynamic Mappings    |
 *                     |              .               |
 *                     :              .               :
 *                     ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ RW/--
 *                     :                              :
 *                     |      Invalid Memory (*)      | --/--
 *    ULIM      ---->  +------------------------------+ 0x80000000      --+
 *                     |  Cur. Page Table (User R-)   | R-/R-  PTSIZE     |
 *    UVPT      ---->  +------------------------------+ 0x7fc00000      --+
 *                     | Unmapped (expandable region) |                   |
 *                     |                              | R-/R-            PTSIZE
 *                     |     Per-Process R/O Info     |                   |
 * UWLIM, UINFO ---->  +------------------------------+ 0x7f800000      --+
 *                     | Unmapped (expandable region) |                   |
 *                     |                              | RW/RW            PTSIZE
 *                     |     Per-Process R/W Data     |                   |
 *    UDATA     ---->  +------------------------------+ 0x7f400000      --+
 *    UMAPTOP,         |    Global Shared R/W Data    | RW/RW  PGSIZE
 *      UGDATA  ---->  +------------------------------+ 0x7f3ff000
 *                     |     User Exception Stack     | RW/RW  PGSIZE
 *                     +------------------------------+ 0x7f3fe000
 *                     |       Empty Memory (*)       | --/--  PGSIZE
 *    USTACKTOP  --->  +------------------------------+ 0x7f3fd000
 *                     |      Normal User Stack       | RW/RW  256*PGSIZE (1MB)
 *                     +------------------------------+ 0x7f2fd000
 *                     |       Empty Memory (*)       | --/--  PGSIZE
 *    USTACKBOT  --->  +------------------------------+ 0x7f2fc000
 *                     |                              |
 *                     |                              |
 *                     ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *                     .                              .
 *                     .                              .
 *                     .                              .
 *                     |~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~|
 *                     |     Program Data & Heap      |
 *    UTEXT -------->  +------------------------------+ 0x00800000
 *                     |                              |
 *                     |       Empty Memory (*)       |
 *                     |                              |
 *                     +------------------------------+ 0x00000000
 *
 * (*) Note: The kernel ensures that "Invalid Memory" (ULIM) is *never*
 *     mapped.  "Empty Memory" is normally unmapped, but user programs may
 *     map pages there if desired.  ROS user programs map pages temporarily
 *     at UTEMP.
 *
 *     KERN_VMAP_TOP is set to the IO_APIC_BASE, where we'll map in the IOAPIC
 *     and LAPIC.  We need to not give out this region as free pages.
 */


// At IOPHYSMEM (640K) there is a 384K hole for I/O.  From the kernel,
// IOPHYSMEM can be addressed at KERNBASE + IOPHYSMEM.  The hole ends
// at physical address EXTPHYSMEM.
#define IOPHYSMEM	0x0A0000
#define VGAPHYSMEM	0x0A0000
#define DEVPHYSMEM	0x0C0000
#define BIOSPHYSMEM	0x0F0000
#define EXTPHYSMEM	0x100000

/* **************************************** */
/* Kernel Virtual Memory Mapping  (not really an MMU thing) */

#define KERNBASE        0xC0000000
#define KERN_LOAD_ADDR  KERNBASE
/* Top of the kernel virtual mapping area (KERNBASE) */
/* For sanity reasons, I don't plan to map the top page */
#define KERN_VMAP_TOP				0xfffff000

/* Static kernel mappings */
/* Virtual page table.  Entry PDX(VPT) in the PD contains a pointer to
 * the page directory itself, thereby turning the PD into a page table,
 * which maps all the PTEs containing the page mappings for the entire
 * virtual address space into that 4 Meg region starting at VPT. */
#define VPT				(KERNBASE - PTSIZE)
#define VPD (VPT + (VPT >> 10))
#define vpd VPD
#define APIC_SIZE 		0x100000
#define LAPIC_BASE		(VPT - APIC_SIZE)
#define IOAPIC_BASE		(LAPIC_BASE - APIC_SIZE)

/* All arches must define this, which is the lower limit of their static
 * mappings, and where the dynamic mappings will start. */
#define KERN_DYN_TOP	IOAPIC_BASE

#define ULIM            0x80000000

/* Same as VPT but read-only for users */
#define UVPT		(ULIM - PTSIZE)

/* Arbitrary boundary between the break and the start of
 * memory returned by calls to mmap with addr = 0 */
#define BRK_END 0x40000000

// Use this if needed in annotations
#define IVY_KERNBASE (0xC000U << 16)

/* **************************************** */
/* Page table constants, macros, etc */

// A linear address 'la' has a three-part structure as follows:
//
// +--------10------+-------10-------+---------12----------+
// | Page Directory |   Page Table   | Offset within Page  |
// |      Index     |      Index     |                     |
// +----------------+----------------+---------------------+
//  \--- PDX(la) --/ \--- PTX(la) --/ \---- PGOFF(la) ----/
//  \----------- PPN(la) -----------/
//
// The PDX, PTX, PGOFF, and PPN macros decompose linear addresses as shown.
// To construct a linear address la from PDX(la), PTX(la), and PGOFF(la),
// use PGADDR(PDX(la), PTX(la), PGOFF(la)).

// page number field of address
#define LA2PPN(la)	(((uintptr_t) (la)) >> PGSHIFT)
#define PTE2PPN(pte)	LA2PPN(pte)

// page directory index
#define PDX(la)		((((uintptr_t) (la)) >> PDXSHIFT) & 0x3FF)

// page table index
#define PTX(la)		((((uintptr_t) (la)) >> PTXSHIFT) & 0x3FF)

// offset in page
#define PGOFF(la)	(((uintptr_t) (la)) & 0xFFF)

// offset in jumbo page
#define JPGOFF(la)	(((uintptr_t) (la)) & 0x003FFFFF)

// construct PTE from PPN and flags
#define PTE(ppn, flags) ((ppn) << PTXSHIFT | PGOFF(flags))

// construct linear address from indexes and offset
#define PGADDR(d, t, o)	((void*SNT) ((d) << PDXSHIFT | (t) << PTXSHIFT | (o)))

// Page directory and page table constants.
#define NPDENTRIES	1024		// page directory entries per page directory
#define NPTENTRIES	1024		// page table entries per page table

#define PTXSHIFT	12		// offset of PTX in a linear address
#define PDXSHIFT	22		// offset of PDX in a linear address

// Page table/directory entry flags.
#define PTE_P		0x001	// Present
#define PTE_W		0x002	// Writeable
#define PTE_U		0x004	// User
#define PTE_PWT		0x008	// Write-Through
#define PTE_PCD		0x010	// Cache-Disable
#define PTE_A		0x020	// Accessed
#define PTE_D		0x040	// Dirty
#define PTE_PS		0x080	// Page Size (only applies to PDEs)
#define PTE_PAT		0x080	// PAT (only applies to second layer PTEs)
#define PTE_G		0x100	// Global Page

#define PTE_PERM	(PTE_W | PTE_U) // The permissions fields
// commly used access modes
#define PTE_KERN_RW	PTE_W		// Kernel Read/Write
#define PTE_KERN_RO	0		// Kernel Read-Only
#define PTE_USER_RW	(PTE_W | PTE_U)	// Kernel/User Read/Write
#define PTE_USER_RO	PTE_U		// Kernel/User Read-Only

// The PTE_AVAIL bits aren't used by the kernel or interpreted by the
// hardware, so user processes are allowed to set them arbitrarily.
#define PTE_AVAIL	0xE00	// Available for software use

// Only flags in PTE_USER may be used in system calls.
#define PTE_USER	(PTE_AVAIL | PTE_P | PTE_W | PTE_U)

// address in page table entry
#define PTE_ADDR(pte)	((physaddr_t) (pte) & ~0xFFF)

#define PTSHIFT 22
#define PTSIZE (1 << PTSHIFT)
#define PGSHIFT 12
#define PGSIZE (1 << PGSHIFT)
#define JPGSIZE PTSIZE

// we must guarantee that for any PTE, exactly one of the following is true
#define PAGE_PRESENT(pte) ((pte) & PTE_P)
#define PAGE_UNMAPPED(pte) ((pte) == 0)
#define PAGE_PAGED_OUT(pte) (!PAGE_PRESENT(pte) && !PAGE_UNMAPPED(pte))

/* **************************************** */
/* Segmentation */

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
typedef struct Segdesc syssegdesc_t;

// Null segment
#define SEG_NULL	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }
// Segment that is loadable but faults when used
#define SEG_FAULT	{ 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 1, 0, 0 }
// Normal segment
#define SEG(type, base, lim, dpl) 									\
{ ((lim) >> 12) & 0xffff, (base) & 0xffff, ((base) >> 16) & 0xff,	\
    type, 1, dpl, 1, (unsigned) (lim) >> 28, 0, 0, 1, 1,			\
    (unsigned) (base) >> 24 }

#define SEG16(type, base, lim, dpl) 								\
{ (lim) & 0xffff, (base) & 0xffff, ((base) >> 16) & 0xff,			\
    type, 1, dpl, 1, (unsigned) (lim) >> 16, 0, 0, 1, 0,			\
    (unsigned) (base) >> 24 }

// System segment (LDT)
#define SEG_SYS(type, base, lim, dpl) 								\
{ ((lim) >> 12) & 0xffff, (base) & 0xffff, ((base) >> 16) & 0xff,	\
    type, 0, dpl, 1, (unsigned) (lim) >> 28, 0, 0, 1, 1,			\
    (unsigned) (base) >> 24 }

#define SEG16_SYS(type, base, lim, dpl) 							\
{ (lim) & 0xffff, (base) & 0xffff, ((base) >> 16) & 0xff,			\
    type, 0, dpl, 1, (unsigned) (lim) >> 16, 0, 0, 1, 0,			\
    (unsigned) (base) >> 24 }

#define SEG_SYS_SMALL(type, base, lim, dpl) \
        SEG16_SYS(type, base, lim, dpl)

// Task state segment format (as described by the Pentium architecture book)
typedef struct taskstate {
	uint32_t ts_link;	// Old ts selector
	uintptr_t ts_esp0;	// Stack pointers and segment selectors
	uint16_t ts_ss0;	//   after an increase in privilege level
	uint16_t ts_padding1;
	uintptr_t ts_esp1;
	uint16_t ts_ss1;
	uint16_t ts_padding2;
	uintptr_t ts_esp2;
	uint16_t ts_ss2;
	uint16_t ts_padding3;
	physaddr_t ts_cr3;	// Page directory base
	uintptr_t ts_eip;	// Saved state from last task switch
	uint32_t ts_eflags;
	uint32_t ts_eax;	// More saved state (registers)
	uint32_t ts_ecx;
	uint32_t ts_edx;
	uint32_t ts_ebx;
	uintptr_t ts_esp;
	uintptr_t ts_ebp;
	uint32_t ts_esi;
	uint32_t ts_edi;
	uint16_t ts_es;		// Even more saved state (segment selectors)
	uint16_t ts_padding4;
	uint16_t ts_cs;
	uint16_t ts_padding5;
	uint16_t ts_ss;
	uint16_t ts_padding6;
	uint16_t ts_ds;
	uint16_t ts_padding7;
	uint16_t ts_fs;
	uint16_t ts_padding8;
	uint16_t ts_gs;
	uint16_t ts_padding9;
	uint16_t ts_ldt;
	uint16_t ts_padding10;
	uint16_t ts_t;		// Trap on task switch
	uint16_t ts_iomb;	// I/O map base address
} taskstate_t;

// Gate descriptors for interrupts and traps
typedef struct Gatedesc {
	unsigned gd_off_15_0 : 16;   // low 16 bits of offset in segment
	unsigned gd_ss : 16;         // segment selector
	unsigned gd_args : 5;        // # args, 0 for interrupt/trap gates
	unsigned gd_rsv1 : 3;        // reserved(should be zero I guess)
	unsigned gd_type : 4;        // type(STS_{TG,IG32,TG32})
	unsigned gd_s : 1;           // must be 0 (system)
	unsigned gd_dpl : 2;         // DPL - highest ring allowed to use this
	unsigned gd_p : 1;           // Present
	unsigned gd_off_31_16 : 16;  // high bits of offset in segment
} gatedesc_t;

// Set up a normal interrupt/trap gate descriptor.
// - istrap: 1 for a trap (= exception) gate, 0 for an interrupt gate.
//   - interrupt gates automatically disable interrupts (cli)
// - sel: Code segment selector for interrupt/trap handler
// - off: Offset in code segment for interrupt/trap handler
// - dpl: Descriptor Privilege Level -
//	  the privilege level required for software to invoke
//	  this interrupt/trap gate explicitly using an int instruction.
#define SETGATE(gate, istrap, sel, off, dpl)			\
{								\
	(gate).gd_off_15_0 = (uint32_t) (off) & 0xffff;		\
	(gate).gd_ss = (sel);					\
	(gate).gd_args = 0;					\
	(gate).gd_rsv1 = 0;					\
	(gate).gd_type = (istrap) ? STS_TG32 : STS_IG32;	\
	(gate).gd_s = 0;					\
	(gate).gd_dpl = (dpl);					\
	(gate).gd_p = 1;					\
	(gate).gd_off_31_16 = (uint32_t) (off) >> 16;		\
}

#define ROSETGATE(gate, istrap, sel, off, dpl)			\
{								\
	(gate).gd_off_15_0 = SINIT((uint32_t) (off) & 0xffff);		\
	(gate).gd_ss = SINIT(sel);					\
	(gate).gd_args = SINIT(0);					\
	(gate).gd_rsv1 = SINIT(0);					\
	(gate).gd_type = SINIT((istrap) ? STS_TG32 : STS_IG32);	\
	(gate).gd_s = SINIT(0);					\
	(gate).gd_dpl = SINIT(dpl);					\
	(gate).gd_p = SINIT(1);					\
	(gate).gd_off_31_16 = SINIT((uint32_t) (off) >> 16);		\
}

// Set up a call gate descriptor.
#define SETCALLGATE(gate, ss, off, dpl)           	        \
{								\
	(gate).gd_off_15_0 = (uint32_t) (off) & 0xffff;		\
	(gate).gd_ss = (ss);					\
	(gate).gd_args = 0;					\
	(gate).gd_rsv1 = 0;					\
	(gate).gd_type = STS_CG32;				\
	(gate).gd_s = 0;					\
	(gate).gd_dpl = (dpl);					\
	(gate).gd_p = 1;					\
	(gate).gd_off_31_16 = (uint32_t) (off) >> 16;		\
}

// Pseudo-descriptors used for LGDT, LLDT and LIDT instructions.
typedef struct Pseudodesc {
	uint16_t pd_lim;		// Limit
	uint32_t pd_base;		// Base address
} __attribute__ ((packed)) pseudodesc_t;

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
#endif /* ROS_INC_ARCH_MMU32_H */
