#ifndef ROS_ARCH_MMU_H
#define ROS_ARCH_MMU_H

/*
 * This file contains definitions for the RISC-V MMU.
 */

#include <ros/arch/mmu.h>

/*
 *
 *	Part 1.  Paging data structures and constants.
 *
 */

// RV64 virtual addresses are 48 bits, sign-extended out to 64 bits,
// creating a hole between 0x0000 7FFF FFFF FFFF and 0xFFFF 8000 0000 0000.
// Bits 11-0 are the page offset; L1/L2/L3/L4 page table indices are given
// by bits 47-39, 38-30, 29-21, and 20-12, respectively.
//
// In RV32, virtual addresses are 32 bits; bits 11-0 are the page offset;
// and L1/L2 page table indices are given by bits 31-22 and 21-12,
// respectively.
//
// In both cases, the last-level page size is 4KB, as is the page table size.

// page number field of address
#define LA2PPN(la)	(((uintptr_t) (la)) >> PGSHIFT)

// page number field of PPN
#define PTE2PPN(pte)	(((uintptr_t) (pte)) >> PTE_PPN_SHIFT)

// index into L1 PT
#define L1X(la)		((((uintptr_t) (la)) >> L1PGSHIFT) & (NPTENTRIES-1))

// index into L2 PT
#define L2X(la)		((((uintptr_t) (la)) >> L2PGSHIFT) & (NPTENTRIES-1))

#ifdef __riscv64
// index into L3 PT
#define L3X(la)		((((uintptr_t) (la)) >> L3PGSHIFT) & (NPTENTRIES-1))

// index into L4 PT
#define L4X(la)		((((uintptr_t) (la)) >> L4PGSHIFT) & (NPTENTRIES-1))

// construct linear address from indexes and offset
#define PGADDR(l1, l2, l3, l4, o) ((uintptr_t) ((l1) << L1PGSHIFT | (l2) << L2PGSHIFT | (l3) << L3PGSHIFT | (l4) << L4PGSHIFT | (o)))
#else
// construct linear address from indexes and offset
#define PGADDR(l1, l2, o) ((uintptr_t) ((l1) << L1PGSHIFT | (l2) << L2PGSHIFT | (o)))
#endif

// offset in page
#define PGOFF(la)	(((uintptr_t) (la)) & (PGSIZE-1))

// construct PTE from PPN and flags
#define PTE(ppn, flags) ((ppn) << PTE_PPN_SHIFT | (flags))

// construct PTD from physical address
#define PTD(pa) ((uintptr_t)(pa) | PTE_T)

// Page directory and page table constants
#define NPTENTRIES (PTSIZE/sizeof(pte_t))

// Page table/directory entry flags.
#define PTE_T    0x001 // Entry is a page Table descriptor
#define PTE_E    0x002 // Entry is a page table Entry
#define PTE_R    0x004 // Referenced
#define PTE_D    0x008 // Dirty
#define PTE_UX   0x010 // User eXecute permission
#define PTE_UW   0x020 // User Read permission
#define PTE_UR   0x040 // User Write permission
#define PTE_SX   0x080 // Supervisor eXecute permission
#define PTE_SW   0x100 // Supervisor Read permission
#define PTE_SR   0x200 // Supervisor Write permission
#define PTE_PERM (PTE_SR | PTE_SW | PTE_SX | PTE_UR | PTE_UW | PTE_UX)
#define PTE_PPN_SHIFT 12

// commly used access modes
#define PTE_KERN_RW	(PTE_SR | PTE_SW | PTE_SX)
#define PTE_KERN_RO	(PTE_SR | PTE_SX)
#define PTE_USER_RW	(PTE_SR | PTE_SW | PTE_UR | PTE_UW | PTE_UX)
#define PTE_USER_RO	(PTE_SR | PTE_UR | PTE_UX)

// x86 equivalencies
#define PTE_P      PTE_E
#define NPDENTRIES NPTENTRIES
#define PDX(la)    L1X(la)			// for env stuff

// address in page table entry
#define PTE_ADDR(pte)	((physaddr_t) (pte) & (PGSIZE-1))

// address in page table descriptor
#define PTD_ADDR(ptd)	PTE_ADDR(ptd)

// MMU Control Register flags
#define MMU_CR_E	0x00000001	// Protection Enable
#define MMU_CR_NF	0x00000002	// No Fault mode
#define MMU_CR_PSO	0x00000080	// Partial Store Order (TSO disabled)

// MMU Fault Status Register flags
#define MMU_FSR_USER	0x00000020	// Fault caused by user-space access
#define MMU_FSR_EX	0x00000040	// Fault occured in instruction-space
#define MMU_FSR_WR	0x00000080	// Fault caused by a store

// MMU Register Addresses
#define MMU_REG_CTRL	0x00000000	// MMU Control Register
#define MMU_REG_CTXTBL	0x00000100	// MMU Context Table Pointer Register
#define MMU_REG_CTX	0x00000200	// MMU Context Register
#define MMU_REG_FSR	0x00000300	// MMU Fault Status Register
#define MMU_REG_FAR	0x00000400	// MMU Fault Address Register

// we must guarantee that for any PTE, exactly one of the following is true
#define PAGE_PRESENT(pte) ((pte) & PTE_P)
#define PAGE_UNMAPPED(pte) ((pte) == 0)
#define PAGE_PAGED_OUT(pte) (!PAGE_PRESENT(pte) && !PAGE_UNMAPPED(pte))

#endif /* !ROS_INC_MMU_H */
