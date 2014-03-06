#ifndef ROS_INC_ARCH_MMU64_H
#define ROS_INC_ARCH_MMU64_H

#ifndef ROS_INC_ARCH_MMU_H
#error "Do not include include ros/arch/mmu64.h directly"
#endif

#ifndef __ASSEMBLER__
#include <ros/common.h>
typedef unsigned long pte_t;
typedef unsigned long pde_t;
#endif

/* Virtual memory map:                                  Virt Addresses
 *                                                      perms: kernel/user
 *
 *                     +------------------------------+ 0xffffffffffffffff -+
 *                     |                              |                     |
 *                     |   Mapped to lowmem, unused   | RW/--               |
 *                     |                              |                     |
 *  "end" symbol  -->  +------------------------------+        PML3_PTE_REACH
 *                     |                              |                     |
 *                     |  Kernel link/load location   |                     |
 *                     |    (mapped to 0, physical)   |                     |
 *                     |                              |                     |
 * KERN_LOAD_ADDR -->  +------------------------------+ 0xffffffffc0000000 -+
 *                     |                              |
 *                     |          Local APIC          | RW/--  APIC_SIZE (1MB)
 *                     |                              |
 *    LAPIC_BASE  -->  +------------------------------+ 0xffffffffbff00000
 *                     |                              |
 *                     |            IOAPIC            | RW/--  APIC_SIZE (1MB)
 *                     |                              |
 *  IOAPIC_BASE,  -->  +------------------------------+ 0xffffffffbfe00000
 *  KERN_DYN_TOP       |   Kernel Dynamic Mappings    |
 *                     |              .               |
 *                     :              .               :
 *                     ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ RW/--
 *                     :                              :
 *                     |          Unmapped            | --/--
 *                     |                              |
 *                     |  Kernel static linking limit |
 *                     +------------------------------+ 0xffffffff80000000
 *                     |                              |
 *                     |                              |
 *                     |                              |
 *  VPT_TOP    ----->  +------------------------------+ 0xffffff0000000000 -+
 *                     |                              |                     |
 *                     |                              |                     |
 *                     |  Cur. Page Table (Kern. RW)  | RW/--  P4ML_PTE_REACH
 *                     |                              |                     |
 *                     |                              |                     |
 *    VPT,     ----->  +------------------------------+ 0xfffffe8000000000 -+
 *  KERN_VMAP_TOP      |                              |
 *                     :              .               :
 *                     :              .               :
 *                     :              .               :
 *                     |~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~| RW/--
 *                     |                              | RW/--
 *                     |                              | RW/--
 *                     |                              | RW/--
 *                     |   Remapped Physical Memory   | RW/--
 *                     |                              | RW/--
 *                     |                              | RW/--
 *                     |                              | RW/--
 *    KERNBASE  ---->  +------------------------------+ 0xffff800000000000
 *                     |                              |
 *                     |                              |
 *                     |                              |
 *                     |   Non-canonical addresses    |
 *                     |         (unusable)           |
 *                     |                              |
 *                     |                              |
 * ULIM (not canon) -> +------------------------------+ 0x0000800000000000 -+
 *                     +     Highest user address     + 0x00007fffffffffff  |
 *                     |                              |                     |
 *                     |  Cur. Page Table (User R-)   | R-/R-  PML4_PTE_REACH
 *                     |                              |                     |
 *    UVPT      ---->  +------------------------------+ 0x00007f8000000000 -+
 *                     | Unmapped (expandable region) |                     |
 *                     |~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~|                     |
 *                     |     Per-Process R/O Info     | R-/R-  PML2_PTE_REACH
 *                     |         (procinfo)           |                     |
 * UWLIM, UINFO ---->  +------------------------------+ 0x00007f7fffe00000 -+
 *                     | Unmapped (expandable region) |                     |
 *                     |~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~|                     |
 *                     |     Per-Process R/W Data     | RW/RW  PML2_PTE_REACH
 *                     |         (procdata)           |                     |
 *    UDATA     ---->  +------------------------------+ 0x00007f7fffc00000 -+
 *                     |                              |
 *                     |    Global Shared R/W Data    | RW/RW  PGSIZE
 *                     |                              |
 * UMAPTOP, UGDATA ->  +------------------------------+ 0x00007f7fffbff000
 *    USTACKTOP        |                              |
 *                     |      Normal User Stack       | RW/RW 256 * PGSIZE
 *                     |                              |
 *                     +------------------------------+ 0x00007f7fffaff000
 *                     |                              |
 *                     |        Empty Memory          |
 *                     |                              |
 *                     .                              .
 *                     .                              .
 *    BRK_END   ---->  +------------------------------+ 0x0000400000000000
 *                     .                              .
 *                     .                              .
 *                     |                              |
 *                     |        Empty Memory          |
 *                     |                              |
 *                     |~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~|
 *                     |                              |
 *                     |     Program Data & Heap      |
 *                     |                              |
 *                     +------------------------------+ 0x0000000000400000
 *                     |                              |
 *                     |       Empty Memory (*)       |
 *                     |                              |
 *                     +------------------------------+ 0x0000000000000000
 */

/* Physical Mapping symbols:
 * At IOPHYSMEM (640K) there is a 384K hole for I/O.  From the kernel,
 * IOPHYSMEM can be addressed at KERNBASE + IOPHYSMEM.  The hole ends
 * at physical address EXTPHYSMEM. */
#define IOPHYSMEM		0x0A0000
#define VGAPHYSMEM		0x0A0000
#define DEVPHYSMEM		0x0C0000
#define BIOSPHYSMEM		0x0F0000
#define EXTPHYSMEM		0x100000

/* Kernel Virtual Memory Mapping */

/* The kernel needs to be loaded in the top 2 GB of memory, since we compile it
 * with -mcmodel=kernel (helps with relocations).  We're actually loading it in
 * the top 1 GB. */
#define KERN_LOAD_ADDR  0xffffffffc0000000
/* Static kernel mappings */
#define APIC_SIZE 		0x100000
#define LAPIC_BASE		(KERN_LOAD_ADDR - APIC_SIZE)
#define IOAPIC_BASE		(LAPIC_BASE - APIC_SIZE)
/* All arches must define this, which is the lower limit of their static
 * mappings, and where the dynamic mappings will start. */
#define KERN_DYN_TOP	IOAPIC_BASE

/* Virtual page table.  Every PML4 has a PTE at the slot (PML4(VPT))
 * corresponding to VPT that points to that PML4's base.  In essence, the 512
 * GB chunk of the VA space from VPT..VPT_TOP is a window into the paging
 * structure.
 *
 * The VPT needs to be aligned on 39 bits.
 *
 * Ex: Say the VPT's entry in 9 bits is "9v".  If you construct a VA from:
 * 9v9v9v9v000, the paging hardware will recurse 4 times, with the end result
 * being the PML4.  That virtual address will map to the PML4 itself.
 *
 * If you want to see a specific PML3, figure out which entry it is in the
 * PML4 (using PML3(va)), say 9 bits = "9X".  The VA 9v9v9v9X000 will map to
 * that PML3. */
#define VPT_TOP			0xffffff0000000000
#define VPT				(VPT_TOP - PML4_PTE_REACH)
/* Helper to return the current outer pgdir via the VPT mapping. */
#define VPD (VPT + ((VPT & 0x0000ffffffffffff) >> 9) +                         \
                   ((VPT & 0x0000ffffffffffff) >> 18) +                        \
                   ((VPT & 0x0000ffffffffffff) >> 27))
#define vpd VPD

/* Top of the kernel virtual mapping area (KERNBASE) */
#define KERN_VMAP_TOP	(VPT)
/* Base of the physical memory map. This maps from 0 physical to max_paddr */
#define KERNBASE        0xffff800000000000

/* Highest user address: 0x00007fffffffffff: 1 zero, 47 ones, sign extended.
 * From here down to UWLIM is User Read-only */
#define ULIM            0x0000800000000000
/* Same as VPT but read-only for users */
#define UVPT			(ULIM - PML4_PTE_REACH)
/* Arbitrary boundary between the break and the start of
 * memory returned by calls to mmap with addr = 0 */
#define BRK_END			0x0000400000000000

/* **************************************** */
/* Page table constants, macros, etc */

/* A linear address 'la' has a five-part structure as follows:
 *
 * +-----9------+-----9------+-----9------+-----9------+---------12----------+
 * | PML4 bits  | PML3 bits  | PML2 bits  | PML1 bits  |     Page offset     |
 * |   offset   |   offset   |   offset   |   offset   |                     |
 * +------------+------------+------------+------------+---------------------+
 *  \ PML4(la) / \ PML3(la) / \ PML2(la) / \ PML1(la) / \---- PGOFF(la) ----/
 *  \------------------ LA2PPN(la) -------------------/
 *
 * The PMLx, PGOFF, and LA2PPN macros decompose linear addresses as shown.
 * To construct a linear address la from these, use:
 * PGADDR(PML4(la), PML3(la), PML2(la), PML1(la), PGOFF(la)).
 * Careful, that's arch- and bit-specific.
 *
 * I'd somewhat like it if we started counting from the outer-most PT, though
 * amd coined the term PML4 for the outermost, instead of PML1.  Incidentally,
 * they also don't use numbers other than PML4, sticking with names like PDP. */

#define PML4_SHIFT		39
#define PML3_SHIFT		30
#define PML2_SHIFT		21
#define PML1_SHIFT		12
#define BITS_PER_PML	9

/* PTE reach is the amount of VM an entry can map, either as a jumbo or as
 * further page tables.  I'd like to write these as shifts, but I can't please
 * both the compiler and the assembler. */
#define PML4_PTE_REACH	(0x0000008000000000)	/* No jumbos available */
#define PML3_PTE_REACH	(0x0000000040000000)	/* 1 GB jumbos available */
#define PML2_PTE_REACH	(0x0000000000200000)	/* 2 MB jumbos available */
#define PML1_PTE_REACH	(0x0000000000001000)	/* aka, PGSIZE */

/* Reach is the amount of VM a table can map, counting all of its entries.
 * Note that a PML(n)_PTE is a PML(n-1) table. */
#define PML3_REACH		(PML4_PTE_REACH)
#define PML2_REACH		(PML3_PTE_REACH)
#define PML1_REACH		(PML2_PTE_REACH)

/* PMLx(la, shift) gives the 9 bits specifying the la's entry in the PML
 * corresponding to shift.  PMLn(la) gives the 9 bits for PML4, etc. */
#define PMLx(la, shift)	(((uintptr_t)(la) >> (shift)) & 0x1ff)
#define PML4(la) 		PMLx(la, PML4_SHIFT)
#define PML3(la) 		PMLx(la, PML3_SHIFT)
#define PML2(la) 		PMLx(la, PML2_SHIFT)
#define PML1(la) 		PMLx(la, PML1_SHIFT)

/* Common kernel helpers */
#define PGSHIFT			PML1_SHIFT
#define PGSIZE			PML1_PTE_REACH
#define LA2PPN(la)		((uintptr_t)(la) >> PGSHIFT)
#define PTE2PPN(pte)	LA2PPN(pte)
#define PGOFF(la)		((uintptr_t)(la) & (PGSIZE - 1))

/* construct PTE from PPN and flags */
#define PTE(ppn, flags) ((ppn) << PGSHIFT | PGOFF(flags))

/* construct linear address from indexes and offset */
#define PGADDR(p4, p3, p2, p1, o) ((void*)(((p4) << PML4_SHIFT) |              \
                                           ((p3) << PML3_SHIFT) |              \
                                           ((p2) << PML2_SHIFT) |              \
                                           ((p1) << PML1_SHIFT) |(o)))

/* These are used in older code, referring to the outer-most page table */
#define PDX(la)			PML4(la)
#define NPDENTRIES		512
/* This is used in places (procinfo) meaning "size of smallest jumbo page" */
#define PTSIZE PML2_PTE_REACH


/* TODO: not sure if we'll need these - limited to 64bit code */
/* this only gives us the L1 PML */
#define PTX(la)		((((uintptr_t) (la)) >> 12) & 0x1ff)
#define JPGOFF(la)	(((uintptr_t) (la)) & 0x001FFFFF)
#define NPTENTRIES		512
#define JPGSIZE PTSIZE

/* Page table/directory entry flags. */

/* Some things to be careful of:  Global and PAT only apply to the last PTE in
 * a chain: so either a PTE in PML1, or a Jumbo PTE in PML2 or 3.  When PAT
 * applies, which bit we use depends on whether we are jumbo or not.  For PML1,
 * PAT is bit 8.  For jumbo PTEs (and only when they are for a jumbo page), we
 * use bit 12. */
#define PTE_P			0x001	/* Present */
#define PTE_W			0x002	/* Writeable */
#define PTE_U			0x004	/* User */
#define PTE_PWT			0x008	/* Write-Through */
#define PTE_PCD			0x010	/* Cache-Disable */
#define PTE_A			0x020	/* Accessed */
#define PTE_D			0x040	/* Dirty */
#define PTE_PS			0x080	/* Page Size */
#define PTE_PAT			0x080	/* Page attribute table */
#define PTE_G			0x100	/* Global Page */
#define PTE_JPAT		0x800	/* Jumbo PAT */

/* Permissions fields and common access modes.  These should be read as 'just
 * kernel or user too' and 'RO or RW'.  USER_RO means read-only for everyone. */
#define PTE_PERM		(PTE_W | PTE_U)
#define PTE_KERN_RW		PTE_W		// Kernel Read/Write
#define PTE_KERN_RO		0		// Kernel Read-Only
#define PTE_USER_RW		(PTE_W | PTE_U)	// Kernel/User Read/Write
#define PTE_USER_RO		PTE_U		// Kernel/User Read-Only

/* The PTE/translation part of a PTE/virtual(linear) address.  It's used
 * frequently to be the page address of a virtual address.  Note this doesn't
 * work on jumbo PTEs due to the reserved bits.  Jumbo's don't have a PTE_ADDR
 * in them - just a base address of wherever they point. */
#define PTE_ADDR(pte)	((physaddr_t) (pte) & ~(PGSIZE - 1))
/* More meaningful macro, same as PTE_ADDR */
#define PG_ADDR(la) 	((uintptr_t)(la) & ~(PGSIZE - 1))

/* we must guarantee that for any PTE, exactly one of the following is true */
#define PAGE_PRESENT(pte) ((pte) & PTE_P)
#define PAGE_UNMAPPED(pte) ((pte) == 0)
#define PAGE_PAGED_OUT(pte) (!PAGE_PRESENT(pte) && !PAGE_UNMAPPED(pte))


/* **************************************** */
/* Segmentation */

/* Global descriptor numbers */
#define GD_NULL			0x00	/* NULL descriptor */
#define GD_KT			0x08	/* kernel text */
#define GD_KD			0x10	/* kernel data */
/* syscall/sysret wants UD before UT, but KT before KD.  it really wants UT32,
 * UD, UT64.  anyways... */
#define GD_UD			0x18	/* user data */
#define GD_UT			0x20	/* user text */
#define GD_TSS			0x28	/* Task segment selector */
#define GD_TSS2			0x30	/* Placeholder, TSS is 2-descriptors wide */
/* These two aren't in the GDT yet (might never be) */
#define GD_LDT			0x38	/* Local descriptor table */
#define GD_LDT2			0x40	/* Placeholder */

#ifdef __ASSEMBLER__

/* Macros to build GDT entries in assembly. */
#define SEG_NULL						\
	.word 0, 0;						\
	.byte 0, 0, 0, 0

/* 64 bit code segment.  This is for long mode, no compatibility.  If we want
 * to support 32 bit apps later, we'll want to adjust this. */
#define SEG_CODE_64(dpl)                                                    \
	.word 0, 0;                                                             \
	.byte 0;                                                                \
	.byte (((1/*p*/) << 7) | ((dpl) << 5) | 0x18 | ((0/*c*/) << 2));        \
	.byte (((0/*d*/) << 6) | ((1/*l*/) << 5));                              \
	.byte 0;

/* 64 bit data segment.  These are pretty much completely ignored (except if we
 * use them for fs/gs, or compatibility mode */
#define SEG_DATA_64(dpl)                                                    \
	.word 0xffff, 0;                                                        \
	.byte 0;                                                                \
	.byte (0x92 | ((dpl) << 5));                                            \
	.byte 0x8f;                                                             \
	.byte 0;

/* System segments (TSS/LDT) are twice as long as usual (16 bytes). */
#define SEG_SYS_64(type, base, lim, dpl)                                       \
	.word ((lim) & 0xffff);                                                    \
	.word ((base) & 0xffff);                                                   \
	.byte (((base) >> 16) & 0xff);                                             \
	.byte ((1 << 7) | ((dpl) << 5) | (type));                                  \
	.byte (((1/*g*/) << 7) | (((lim) >> 16) & 0xf));                           \
	.byte (((base) >> 24) & 0xff);                                             \
	.quad ((base) >> 32);                                                      \
	.quad 0;

/* Default segment (32 bit style).  Would work for fs/gs, if needed */
#define SEG(type, base, lim)                                                \
	.word (((lim) >> 12) & 0xffff);                                         \
	.word ((base) & 0xffff);                                                \
	.byte (((base) >> 16) & 0xff);                                          \
	.byte (0x90 | (type));                                                  \
	.byte (0xC0 | (((lim) >> 28) & 0xf));                                   \
	.byte (((base) >> 24) & 0xff)

#else	// not __ASSEMBLER__

/* Legacy Segment Descriptor (used for 64 bit data and code) */
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

/* Lower half is similar (more ignored, etc)  to a legacy system descriptor */
struct x86_sysseg64 {
	unsigned sd_lim_15_0 : 16;	/* Low bits of segment limit */
	unsigned sd_base_15_0 : 16;	/* Low bits of segment base address */
	unsigned sd_base_23_16 : 8;	/* Middle bits of segment base address */
	unsigned sd_type : 4;		/* Segment type (see STS_ constants) */
	unsigned sd_s : 1;			/* 0 = system, 1 = application */
	unsigned sd_dpl : 2;		/* Descriptor Privilege Level */
	unsigned sd_p : 1;			/* Present */
	unsigned sd_lim_19_16 : 4;	/* High bits of segment limit */
	unsigned sd_avl : 1;		/* Unused (available for software use) */
	unsigned sd_rsv2 : 2;		/* Reserved */
	unsigned sd_g : 1;			/* Granularity: limit scaled by 4K when set */
	unsigned sd_base_31_24 : 8;	/* 24-31 bits of segment base address */
	unsigned sd_base_63_32;		/* top 32 bits of the base */
	unsigned sd_reserved;		/* some parts must be zero, just zero it all */
};
typedef struct x86_sysseg64 syssegdesc_t;

/* G(ranularity) determines if the limit is shifted */
#define __SEG_SYS64(type, base, lim, dpl, g)                                   \
{ ((lim) >> ((g) * 12)) & 0xffff, (base) & 0xffff, ((base) >> 16) & 0xff,      \
    type, 0, dpl, 1, (unsigned) (lim) >> 28, 0, 0, (g),                        \
    ((unsigned) (base) >> 24) & 0xff,                                          \
    ((unsigned long) (base) >> 32), 0 }

/* Normal system segment descriptor (LDT or TSS). (limit is scaled by 4k) */
#define SEG_SYS64(type, base, lim, dpl)                                        \
        __SEG_SYS64(type, base, lim, dpl, 1)

/* Smaller system segment descriptor (LDT or TSS). */
#define SEG_SYS64_SMALL(type, base, lim, dpl)                                  \
        __SEG_SYS64(type, base, lim, dpl, 0)

#define SEG_SYS_SMALL(type, base, lim, dpl) \
        SEG_SYS64_SMALL(type, base, lim, dpl)

/* 64 bit task state segment (AMD 2:12.2.5) */
typedef struct taskstate {
	uint32_t					ts_rsv1;	/* reserved / ignored */
	uint64_t					ts_rsp0;	/* stack ptr in ring 0 */
	uint64_t					ts_rsp1;	/* stack ptr in ring 1 */
	uint64_t					ts_rsp2;	/* stack ptr in ring 2 */
	uint64_t					ts_rsv2;	/* reserved / ignored */
	uint64_t					ts_ist1;	/* IST stacks: unconditional rsp */
	uint64_t					ts_ist2;	/* check AMD 2:8.9.4 for info */
	uint64_t					ts_ist3;
	uint64_t					ts_ist4;
	uint64_t					ts_ist5;
	uint64_t					ts_ist6;
	uint64_t					ts_ist7;
	uint64_t					ts_rsv3;	/* reserved / ignored */
	uint16_t					ts_rsv4;	/* reserved / ignored */
	uint16_t					ts_iobm;	/* IO base map (offset) */
} __attribute__((packed)) taskstate_t;

/* 64 bit gate descriptors for interrupts and traps */
typedef struct Gatedesc {
	unsigned gd_off_15_0 : 16;	/* low 16 bits of offset in segment */
	unsigned gd_ss : 16;		/* segment selector */
	unsigned gd_ist : 3;		/* interrupt stack table selector (0 = none) */
	unsigned gd_rsv1 : 5;		/* ignored */
	unsigned gd_type : 4;		/* type(STS_{TG,IG32,TG32}) */
	unsigned gd_s : 1;			/* must be 0 (system) */
	unsigned gd_dpl : 2;		/* DPL - highest ring allowed to use this */
	unsigned gd_p : 1;			/* Present */
	unsigned gd_off_31_16 : 16;	/* 16-31 bits of offset in segment */
	unsigned gd_off_63_32;		/* top 32 bits of offset */
	unsigned gd_rsv2;			/* reserved / unsused */
} gatedesc_t;

/* Set up an IST-capable 64 bit interrupt/trap gate descriptor.  IST selects a
 * stack pointer from the interrupt-stack table (in TSS) that will be loaded
 * unconditionally when we hit this gate  - regardless of privelege change. */
#define SETGATE64(gate, istrap, sel, off, dpl, ist)                            \
{                                                                              \
	(gate).gd_off_15_0 = (uintptr_t) (off) & 0xffff;                           \
	(gate).gd_ss = (sel);                                                      \
	(gate).gd_ist = (ist);                                                     \
	(gate).gd_rsv1 = 0;                                                        \
	(gate).gd_type = (istrap) ? STS_TG32 : STS_IG32;                           \
	(gate).gd_s = 0;                                                           \
	(gate).gd_dpl = (dpl);                                                     \
	(gate).gd_p = 1;                                                           \
	(gate).gd_off_31_16 = (uintptr_t) (off) >> 16;                             \
	(gate).gd_off_63_32 = (uintptr_t) (off) >> 32;                             \
	(gate).gd_rsv2 = 0;                                                        \
}

/* Set up a normal, 64 bit interrupt/trap gate descriptor.
 * - istrap: 1 for a trap (= exception) gate, 0 for an interrupt gate.
 *   - interrupt gates automatically disable interrupts (cli)
 * - sel: Code segment selector for interrupt/trap handler
 * - off: Offset in code segment for interrupt/trap handler (address)
 * - dpl: Descriptor Privilege Level -
 *	  the privilege level required for software to invoke
 *	  this interrupt/trap gate explicitly using an int instruction. */
#define SETGATE(gate, istrap, sel, off, dpl)                                   \
        SETGATE64(gate, istrap, sel, off, dpl, 0)

// Pseudo-descriptors used for LGDT, LLDT and LIDT instructions.
typedef struct Pseudodesc {
	uint16_t pd_lim;		// Limit
	uintptr_t pd_base;		// Base address
} __attribute__ ((packed)) pseudodesc_t;

#endif /* !__ASSEMBLER__ */

// Application segment type bits
#define STA_X		0x8	    // Executable segment
#define STA_E		0x4	    // Expand down (non-executable segments)
#define STA_C		0x4	    // Conforming code segment (executable only)
#define STA_W		0x2	    // Writeable (non-executable segments)
#define STA_R		0x2	    // Readable (executable segments)
#define STA_A		0x1	    // Accessed

/* System segment type bits.  All other types are reserved/illegal.  The '32' is
 * mostly a legacy naming - the bits work for both 64 and 32. */
#define STS_LDT		0x2		/* 64-bit Local Descriptor Table  */
#define STS_T32A	0x9		/* Available 64-bit TSS */
#define STS_T32B	0xB		/* Busy 64-bit TSS */
#define STS_CG32	0xC		/* 64-bit Call Gate */
#define STS_IG32	0xE		/* 64-bit Interrupt Gate */
#define STS_TG32	0xF		/* 64-bit Trap Gate */

#define SEG_COUNT	7 		/* Number of GDT segments */
/* TODO: Probably won't use this */
#define LDT_SIZE	(8192 * sizeof(segdesc_t))

/* TLS 'syscall', coupled to trapentry64.S.  Needed a non-canon 'addr' */
#define FASTCALL_SETFSBASE 0xf0f0000000000001

#endif /* ROS_INC_ARCH_MMU64_H */
