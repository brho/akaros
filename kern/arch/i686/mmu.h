#ifndef ROS_ARCH_MMU_H
#define ROS_ARCH_MMU_H

#ifndef __ASSEMBLER__
#include <ros/common.h>
#endif

#include <ros/arch/mmu.h>

/*
 * This file contains definitions for the x86 memory management unit (MMU),
 * including paging- and segmentation-related data structures and constants,
 * the %cr0, %cr4, and %eflags registers, and traps.
 */

/*
 *
 *	Part 1.  Paging data structures and constants.
 *
 */

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
#define LA2PPN(la)	(((uintptr_t) (la)) >> PTXSHIFT)
#define PTE2PPN(pte)	LA2PPN(pte)
#define VPN(la)		PPN(la)		// used to index into vpt[]

// page directory index
#define PDX(la)		((((uintptr_t) (la)) >> PDXSHIFT) & 0x3FF)
#define VPD(la)		PDX(la)		// used to index into vpd[]

// page table index
#define PTX(la)		((((uintptr_t) (la)) >> PTXSHIFT) & 0x3FF)

// offset in page
#define PGOFF(la)	(((uintptr_t) (la)) & 0xFFF)

// offset in jumbo page
#define JPGOFF(la)	(((uintptr_t) (la)) & 0x003FFFFF)

// construct PTE from PPN and flags
#define PTE(ppn, flags) ((ppn) << PTXSHIFT | (flags))

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

// Control Register flags
#define CR0_PE		0x00000001	// Protection Enable
#define CR0_MP		0x00000002	// Monitor coProcessor
#define CR0_EM		0x00000004	// Emulation
#define CR0_TS		0x00000008	// Task Switched
#define CR0_ET		0x00000010	// Extension Type
#define CR0_NE		0x00000020	// Numeric Error
#define CR0_WP		0x00010000	// Write Protect
#define CR0_AM		0x00040000	// Alignment Mask
#define CR0_NW		0x20000000	// Not Writethrough - more tricky than it sounds
#define CR0_CD		0x40000000	// Cache Disable
#define CR0_PG		0x80000000	// Paging

// These two relate to the cacheability (L1, etc) of the page directory
#define CR3_PWT		0x00000008	// Page directory caching write through
#define CR3_PCD		0x00000010	// Page directory caching disabled

#define CR4_VME		0x00000001	// V86 Mode Extensions
#define CR4_PVI		0x00000002	// Protected-Mode Virtual Interrupts
#define CR4_TSD		0x00000004	// Time Stamp Disable
#define CR4_DE		0x00000008	// Debugging Extensions
#define CR4_PSE		0x00000010	// Page Size Extensions
#define CR4_PAE		0x00000020	// Physical Address Extensions
#define CR4_MCE		0x00000040	// Machine Check Enable
#define CR4_PGE		0x00000080	// Global Pages Enabled
#define CR4_PCE		0x00000100	// Performance counter enable
#define CR4_OSFXSR	0x00000200	// OS support for FXSAVE/FXRSTOR
#define CR4_OSXMME	0x00000400	// OS support for unmasked SIMD FP exceptions
#define CR4_VMXE	0x00002000	// VMX enable
#define CR4_SMXE	0x00004000	// SMX enable
#define CR4_OSXSAVE	0x00040000	// XSAVE and processor extended states-enabled

// Eflags register
#define FL_CF		0x00000001	// Carry Flag
#define FL_PF		0x00000004	// Parity Flag
#define FL_AF		0x00000010	// Auxiliary carry Flag
#define FL_ZF		0x00000040	// Zero Flag
#define FL_SF		0x00000080	// Sign Flag
#define FL_TF		0x00000100	// Trap Flag
#define FL_IF		0x00000200	// Interrupt Flag
#define FL_DF		0x00000400	// Direction Flag
#define FL_OF		0x00000800	// Overflow Flag
#define FL_IOPL_MASK	0x00003000	// I/O Privilege Level bitmask
#define FL_IOPL_0	0x00000000	//   IOPL == 0
#define FL_IOPL_1	0x00001000	//   IOPL == 1
#define FL_IOPL_2	0x00002000	//   IOPL == 2
#define FL_IOPL_3	0x00003000	//   IOPL == 3
#define FL_NT		0x00004000	// Nested Task
#define FL_RF		0x00010000	// Resume Flag
#define FL_VM		0x00020000	// Virtual 8086 mode
#define FL_AC		0x00040000	// Alignment Check
#define FL_VIF		0x00080000	// Virtual Interrupt Flag
#define FL_VIP		0x00100000	// Virtual Interrupt Pending
#define FL_ID		0x00200000	// ID flag

// Page fault error codes
#define FEC_PR		0x1	// Page fault caused by protection violation
#define FEC_WR		0x2	// Page fault caused by a write
#define FEC_U		0x4	// Page fault occured while in user mode


/*
 *
 *	Part 2.  Segmentation data structures and constants.
 *
 */

/* Segment descriptor and macros temporarily moved to ros/arch/mmu.h.  Bring
 * them back when fixing x86 TLS vulns (TLSV) */

/*
 *
 *	Part 3.  Traps.
 *
 */

#ifndef __ASSEMBLER__

// Task state segment format (as described by the Pentium architecture book)
typedef struct Taskstate {
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

extern segdesc_t (COUNT(SEG_COUNT) RO gdt)[];
extern pseudodesc_t gdt_pd;

// we must guarantee that for any PTE, exactly one of the following is true
#define PAGE_PRESENT(pte) ((pte) & PTE_P)
#define PAGE_UNMAPPED(pte) ((pte) == 0)
#define PAGE_PAGED_OUT(pte) (!PAGE_PRESENT(pte) && !PAGE_UNMAPPED(pte))

#endif /* !__ASSEMBLER__ */

#endif /* !ROS_ARCH_MMU_H */
