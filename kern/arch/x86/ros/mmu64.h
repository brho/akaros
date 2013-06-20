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
 *                     |          Local APIC          | RW/--  PGSIZE
 *                     |                              |
 *    LAPIC_BASE  -->  +------------------------------+ 0xffffffffbffff000
 *                     |                              |
 *                     |            IOAPIC            | RW/--  PGSIZE
 *                     |                              |
 *  IOAPIC_BASE,  -->  +------------------------------+ 0xffffffffbfffe000
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
 *                     +------------------------------+ 0x00007f7fffbfb000
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
#define LAPIC_BASE		(KERN_LOAD_ADDR - PGSIZE)
#define IOAPIC_BASE		(LAPIC_BASE - PGSIZE)
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
#define MAX_JUMBO_SHIFT PML3_SHIFT


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
// XXX 64b: these all need redone

// Global descriptor numbers
#define GD_NULL   0x00     // NULL descriptor
#define GD_KT     0x08     // kernel text
#define GD_KD     0x10     // kernel data
#define GD_UT     0x18     // user text
#define GD_UD     0x20     // user data
#define GD_TSS    0x28     // Task segment selector
#define GD_LDT    0x30     // local descriptor table

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
#define SEG_DATA_64                                                         \
	.word 0, 0;                                                             \
	.byte 0;                                                                \
	.byte 0x90;                                                             \
	.word 0;

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

/* TODO: consider removing this, if we're just using one asm GDT */
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
		(seg).sd_base_15_0 = SINIT((uintptr_t)(base)&0xffff);\
		(seg).sd_base_23_16 = SINIT(((uintptr_t)(base)>>16)&0xff);\
		(seg).sd_type = SINIT(type);\
		(seg).sd_s = SINIT(1);\
		(seg).sd_dpl = SINIT(dpl);\
		(seg).sd_p = SINIT(1);\
		(seg).sd_lim_19_16 = SINIT((unsigned)(lim)>>16);\
		(seg).sd_avl = SINIT(0);\
		(seg).sd_rsv1 = SINIT(0);\
		(seg).sd_db = SINIT(1);\
		(seg).sd_g = SINIT(0);\
		(seg).sd_base_31_24 = SINIT((uintptr_t)(base)>> 24);\
	}

// Task state segment format (as described by the Pentium architecture book)
typedef struct Taskstate {
	uintptr_t ts_link;	// Old ts selector
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
	uintptr_t ts_eflags;
	uintptr_t ts_eax;	// More saved state (registers)
	uintptr_t ts_ecx;
	uintptr_t ts_edx;
	uintptr_t ts_ebx;
	uintptr_t ts_esp;
	uintptr_t ts_ebp;
	uintptr_t ts_esi;
	uintptr_t ts_edi;
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
	(gate).gd_off_15_0 = (uintptr_t) (off) & 0xffff;		\
	(gate).gd_ss = (sel);					\
	(gate).gd_args = 0;					\
	(gate).gd_rsv1 = 0;					\
	(gate).gd_type = (istrap) ? STS_TG32 : STS_IG32;	\
	(gate).gd_s = 0;					\
	(gate).gd_dpl = (dpl);					\
	(gate).gd_p = 1;					\
	(gate).gd_off_31_16 = (uintptr_t) (off) >> 16;		\
}

#define ROSETGATE(gate, istrap, sel, off, dpl)			\
{								\
	(gate).gd_off_15_0 = SINIT((uintptr_t) (off) & 0xffff);		\
	(gate).gd_ss = SINIT(sel);					\
	(gate).gd_args = SINIT(0);					\
	(gate).gd_rsv1 = SINIT(0);					\
	(gate).gd_type = SINIT((istrap) ? STS_TG32 : STS_IG32);	\
	(gate).gd_s = SINIT(0);					\
	(gate).gd_dpl = SINIT(dpl);					\
	(gate).gd_p = SINIT(1);					\
	(gate).gd_off_31_16 = SINIT((uintptr_t) (off) >> 16);		\
}

// Set up a call gate descriptor.
#define SETCALLGATE(gate, ss, off, dpl)           	        \
{								\
	(gate).gd_off_15_0 = (uintptr_t) (off) & 0xffff;		\
	(gate).gd_ss = (ss);					\
	(gate).gd_args = 0;					\
	(gate).gd_rsv1 = 0;					\
	(gate).gd_type = STS_CG32;				\
	(gate).gd_s = 0;					\
	(gate).gd_dpl = (dpl);					\
	(gate).gd_p = 1;					\
	(gate).gd_off_31_16 = (uintptr_t) (off) >> 16;		\
}

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

#endif /* ROS_INC_ARCH_MMU64_H */
