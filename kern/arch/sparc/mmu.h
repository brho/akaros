#ifndef ROS_INC_MMU_H
#define ROS_INC_MMU_H

/*
 * This file contains definitions for the SRMMU.
 */

#include <ros/arch/mmu.h>

/*
 *
 *	Part 1.  Paging data structures and constants.
 *
 */

// A linear address 'la' has a four-part structure as follows:
//
// +--------8--------+------6------+------6------+-----------12----------+
// |  L1 Page Table  |    L2 PT    |    L3 PT    |  Offset within Page   |
// |      Index      |    Index    |    Index    |                       |
// +-----------------+-------------+-------------+-----------------------+
//  \--- L1X(la) --/  \- L2X(la) -/ \- L3X(la) -/ \----- PGOFF(la) -----/
//  \----------- PPN(la) -----------------------/
//
// The L1X, L2X, L3X, PGOFF, and PPN macros decompose linear addresses
// as shown.  To construct a linear address la from L1X(la), L2X(la),
// and PGOFF(la), use PGADDR(L1X(la), L2X(la), L3X(la), PGOFF(la)).

// page number field of address
#define LA2PPN(la)	(((uintptr_t) (la)) >> L3PGSHIFT)

// page number field of PPN
#define PTE2PPN(pte)	(((uintptr_t) (pte)) >> 8)

// index into L1 PT
#define L1X(la)		((((uintptr_t) (la)) >> L1PGSHIFT) & 0xFF)

// index into L2 PT
#define L2X(la)		((((uintptr_t) (la)) >> L2PGSHIFT) & 0x3F)

// index into L3 PT
#define L3X(la)		((((uintptr_t) (la)) >> L3PGSHIFT) & 0x3F)

// offset in page
#define PGOFF(la)	(((uintptr_t) (la)) & 0xFFF)

// construct linear address from indexes and offset
#define PGADDR(l1, l2, l3, o) ((void*SNT) ((l1) << L1PGSHIFT | (l2) << L2PGSHIFT | (l3) << L3PGSHIFT | (o)))

// construct PTE from PPN and flags
#define PTE(ppn, flags) ((ppn) << 8 | (flags))

// construct PTD from physical address
#define PTD(pa) ((pa) >> 4 | PTE_PTD)

// Number of L1 page tables (contexts) the MMU can store at any time
#define NCONTEXTS	8
#define CONTEXT_TABLE_PAD 8 // we require NCONTEXTS+CONTEXT_TBALE_PAD % 16 == 0

// Page directory and page table constants.
#define NL3ENTRIES	64		// # entries in an L3 page table
#define NL2ENTRIES	64		// # entries in an L2 page table
#define NL1ENTRIES	256		// # entries in an L1 page table

// Page table/directory entry flags.
#define PTE_PTD		0x001	// Entry is a Page Table Descriptor
#define PTE_PTE		0x002	// Entry is a Page Table Entry
#define PTE_ACC		0x01C	// Access modes (aka permissions, see below)
#define PTE_R		0x020	// Referenced
#define PTE_M		0x040	// Modified
#define PTE_C		0x080	// Cacheable

// commly used access modes
#define PTE_KERN_RW	(7 << 2)		// Kernel Read/Write
#define PTE_KERN_RO	(6 << 2)		// Kernel Read-Only
#define PTE_USER_RW	(3 << 2)		// Kernel/User Read/Write
#define PTE_USER_RO	(2 << 2)		// Kernel/User Read-Only

// x86 equivalencies
#define PTE_P		PTE_PTE			// present <=> PTE
#define PTE_PERM	PTE_ACC			// perms <=> ACC
#define NPDENTRIES	NL1ENTRIES		// to calculate size of pgdir
#define PDX(la)		L1X(la)			// for env stuff

// +-----+-------------------+
// |     |   Allowed Access  |
// | ACC +------+------------+
// |     | User | Supervisor |
// +-----+------+------------+
// |  0  |  R-- |  R--       |
// |  1  |  RW- |  RW-       |
// |  2  |  R-X |  R-X       |
// |  3  |  RWX |  RWX       |
// |  4  |  --X |  --X       |
// |  5  |  R-- |  RW-       |
// |  6  |  --- |  R-X       |
// |  7  |  --- |  RWX       |
// +-----+------+------------+

// address in page table entry
#define PTE_ADDR(pte)	(((physaddr_t) (pte) & ~0xFF) << 4)

// address in page table descriptor
#define PTD_ADDR(ptd)	(((physaddr_t) (ptd) & ~0x3) << 4)

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

// get the pfault_info pointer stored in this PTE.
// useless unless PAGE_PAGED_OUT(pte).
#define PTE2PFAULT_INFO(pte) ((struct pfault_info*)pte)
// convert a pfault_info pointer to a PTE.
// assumes the pointer is 4-byte aligned.
#define PFAULT_INFO2PTE(ptr) ((pte_t)ptr)

#endif /* !ROS_INC_MMU_H */
