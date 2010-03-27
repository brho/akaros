#ifndef ROS_INC_MEMLAYOUT_H
#define ROS_INC_MEMLAYOUT_H

#ifndef __ASSEMBLER__
#include <ros/common.h>
#endif /* not __ASSEMBLER__ */

#include <ros/arch/mmu.h>

/*
 * This file contains definitions for memory management in our OS,
 * which are relevant to both the kernel and user-mode software.
 */

/*
 * Virtual memory map:                                Permissions
 *                                                    kernel/user
 *
 *    4 Gig -------->  +------------------------------+
 *                     |                              | RW/--
 *                     ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *                     :              .               :
 *                     :              .               :
 *                     :              .               :
 *                     |~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~| RW/--
 *                     |                              | RW/--
 *                     |   Remapped Physical Memory   | RW/--
 *                     |                              | RW/--
 *    KERNBASE ----->  +------------------------------+ 0xc0000000
 *                     |  Cur. Page Table (Kern. RW)  | RW/--  PTSIZE
 *    VPT,KSTACKTOP--> +------------------------------+ 0xbfc00000      --+
 *                     |         Kernel Stack         | RW/--  KSTKSIZE   |
 *                     | - - - - - - - - - - - - - - -|                 PTSIZE
 *                     |      Invalid Memory (*)      | --/--             |
 *    ULIM      ---->  +------------------------------+ 0xbf800000      --+
 *                     |  Cur. Page Table (User R-)   | R-/R-  PTSIZE
 *    UVPT      ---->  +------------------------------+ 0xbf400000      --+
 *                     | Unmapped (expandable region) |                   |
 *                     |                              | R-/R-            PTSIZE
 *                     |     Per-Process R/O Info     |                   |
 * UTOP, UINFO  ---->  +------------------------------+ 0xbf000000      --+
 *                     | Unmapped (expandable region) |                   |
 *                     |                              | RW/RW            PTSIZE
 *                     |     Per-Process R/W Data     |                   |
 *    UDATA     ---->  +------------------------------+ 0xbec00000      --+
 *                     |    Global Shared R/W Data    | RW/RW  PGSIZE
 * UXSTACKTOP,UGDATA ->+------------------------------+ 0xbebff000
 *                     |     User Exception Stack     | RW/RW  PGSIZE
 *                     +------------------------------+ 0xbebfe000
 *                     |       Empty Memory (*)       | --/--  PGSIZE
 *    USTACKTOP  --->  +------------------------------+ 0xbebfd000
 *                     |      Normal User Stack       | RW/RW  256*PGSIZE (1MB)
 *                     +------------------------------+ 0xbeafd000
 *                     |       Empty Memory (*)       | --/--  PGSIZE
 *    USTACKBOT  --->  +------------------------------+ 0xbeafc000
 *                     |                              |
 *                     |                              |
 *                     ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *                     .                              .
 *                     .                              .
 *                     .                              .
 *                     |~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~|
 *                     |     Program Data & Heap      |
 *    UTEXT -------->  +------------------------------+ 0x00800000
 *    PFTEMP ------->  |       Empty Memory (*)       |        PTSIZE
 *                     |                              |
 *    UTEMP -------->  +------------------------------+ 0x00400000      --+
 *                     |       Empty Memory (*)       |                   |
 *                     | - - - - - - - - - - - - - - -|                   |
 *                     |  User STAB Data (optional)   |                 PTSIZE
 *    USTABDATA ---->  +------------------------------+ 0x00200000        |
 *                     |       Empty Memory (*)       |                   |
 *    0 ------------>  +------------------------------+                 --+
 *
 * (*) Note: The kernel ensures that "Invalid Memory" (ULIM) is *never*
 *     mapped.  "Empty Memory" is normally unmapped, but user programs may
 *     map pages there if desired.  ROS user programs map pages temporarily
 *     at UTEMP.
 */


// At IOPHYSMEM (640K) there is a 384K hole for I/O.  From the kernel,
// IOPHYSMEM can be addressed at KERNBASE + IOPHYSMEM.  The hole ends
// at physical address EXTPHYSMEM.
#define IOPHYSMEM	0x0A0000
#define VGAPHYSMEM	0x0A0000
#define DEVPHYSMEM	0x0C0000
#define BIOSPHYSMEM	0x0F0000
#define EXTPHYSMEM	0x100000

// Virtual page table.  Entry PDX(VPT) in the PD contains a pointer to
// the page directory itself, thereby turning the PD into a page table,
// which maps all the PTEs containing the page mappings for the entire
// virtual address space into that 4 Meg region starting at VPT.
#define VPT		(KERNBASE - PTSIZE)
#define KSTACKTOP	VPT
#define KSTKSHIFT	(PGSHIFT+3)		// KSTKSIZE == 8*PGSIZE
#define KSTKSIZE	(1 << KSTKSHIFT)	// size of a kernel stack
#define ULIM		(KSTACKTOP - PTSIZE)

/*
 * User read-only mappings! Anything below here til UTOP are readonly to user.
 * They are global pages mapped in at env allocation time.
 */

// Same as VPT but read-only for users
#define UVPT		(ULIM - PTSIZE)

/*
 * Top of user VM. User can manipulate VA from UTOP-1 and down!
 */

// Top of user-accessible VM
#define UTOP		(UVPT - PTSIZE)
// Read-only, per-process shared info structures
#define UINFO		UTOP

// Read-write, per-process shared page for sending asynchronous 
// syscalls to the kernel
#define UDATA    (UTOP - PTSIZE)

// Read-write, global page.  Shared by all processes.  Can't be trusted.
#define UGDATA   (UDATA - PGSIZE)

// Top of one-page user exception stack
#define UXSTACKTOP	UGDATA
// Next page left invalid to guard against exception stack overflow; then:
// Top of normal user stack
#define USTACKTOP	(UXSTACKTOP - 2*PGSIZE)
// Maximum stack depth preallocated to 1MB
#define USTACK_NUM_PAGES	256
// Next page left invalid to guard against stack overflow
// Maximum bottom of normal user stack
#define USTACKBOT	(USTACKTOP - (USTACK_NUM_PAGES+1)*PGSIZE)

#define UMMAP_NUM_PAGES	131072
#define UMMAP_START	(USTACKBOT - UMMAP_NUM_PAGES*PGSIZE)

// Where user programs generally begin
#define UTEXT		(2*PTSIZE)

// Used for temporary page mappings.  Typed 'void*' for convenience
#define UTEMP		((void*) PTSIZE)
// Used for temporary page mappings for the user page-fault handler
// (should not conflict with other temporary page mappings)
#define PFTEMP		(UTEMP + PTSIZE - PGSIZE)
// The location of the user-level STABS data structure
#define USTABDATA	(PTSIZE / 2)


#ifndef __ASSEMBLER__

/*
 * The page directory entry corresponding to the virtual address range
 * [VPT, VPT + PTSIZE) points to the page directory itself.  Thus, the page
 * directory is treated as a page table as well as a page directory.
 *
 * One result of treating the page directory as a page table is that all PTEs
 * can be accessed through a "virtual page table" at virtual address VPT (to
 * which vpt is set in entry.S).  The PTE for page number N is stored in
 * vpt[N].  (It's worth drawing a diagram of this!)
 *
 * A second consequence is that the contents of the current page directory
 * will always be available at virtual address (VPT + (VPT >> PGSHIFT)), to
 * which vpd is set in entry.S.
 */

#ifdef __IVY__
#pragma cilnoremove("vpt_lock", "vpd_lock")
#endif
extern volatile uint32_t vpt_lock;
extern volatile uint32_t vpd_lock;

extern volatile pte_t SLOCKED(&vpt_lock) (COUNT(PTSIZE) SREADONLY vpt)[]; // VA of "virtual page table"
extern volatile pde_t SLOCKED(&vpd_lock) (COUNT(PTSIZE) SREADONLY vpd)[]; // VA of current page directory

#endif /* !__ASSEMBLER__ */
#endif /* !ROS_INC_MEMLAYOUT_H */
