#ifndef ROS_INC_MEMLAYOUT_H
#define ROS_INC_MEMLAYOUT_H

#ifndef __ASSEMBLER__
#include <arch/types.h>
#include <arch/mmu.h>
#include <ros/queue.h>
#endif /* not __ASSEMBLER__ */

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
 *    ULIM     ------> +------------------------------+ 0xbf800000      --+
 *                     |  Cur. Page Table (User R-)   | R-/R-  PTSIZE
 *    UVPT      ---->  +------------------------------+ 0xbf400000
 *                     |          RO PAGES            | R-/R-  PTSIZE
 *    UPAGES    ---->  +------------------------------+ 0xbf000000      --+
 *                     |  Unmapped (future expansion) | --/--             |
 *                     +------------------------------+ 0xbec01000      PTSIZE
 *                     |     Per-Process R/O Info     | R-/R-  PGSIZE     |
 * UTOP, UINFO  ---->  +------------------------------+ 0xbec00000      --+
 *                     |  Unmapped (future expansion) | --/--             |
 *                     +------------------------------+ 0xbe801000      PTSIZE
 *                     |     Per-Process R/W Data     | RW/RW  PGSIZE     |
 * UDATA,UXSTACKTOP--> +------------------------------+ 0xbe800000      --+
 *                     |     User Exception Stack     | RW/RW  PGSIZE
 *                     +------------------------------+ 0xbe7ff000
 *                     |       Empty Memory (*)       | --/--  PGSIZE
 *    USTACKTOP  --->  +------------------------------+ 0xbe7fe000
 *                     |      Normal User Stack       | RW/RW  PGSIZE
 *                     +------------------------------+ 0xbe7fd000
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


// All physical memory mapped at this address
#define	KERNBASE	0xC0000000

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
#define VPT			(KERNBASE - PTSIZE)
#define KSTACKTOP	VPT
#define KSTKSIZE	(8*PGSIZE)   		// size of a kernel stack
#define ULIM		(KSTACKTOP - PTSIZE) 

/*
 * User read-only mappings! Anything below here til UTOP are readonly to user.
 * They are global pages mapped in at env allocation time.
 */

// Same as VPT but read-only for users
#define UVPT		(ULIM - PTSIZE)
// Read-only copies of the Page structures
#define UPAGES		(UVPT - PTSIZE)
// Read-only, per-process shared info structures
#define UINFO		(UPAGES - PTSIZE)
#define UINFO_PAGES 1

/*
 * Top of user VM. User can manipulate VA from UTOP-1 and down!
 */

// Top of user-accessible VM
#define UTOP		UINFO

// Read-write, per-process shared data structures
#define UDATA		(UTOP - PTSIZE)
#define UDATA_PAGES 1

// Top of one-page user exception stack
#define UXSTACKTOP	UDATA
// Next page left invalid to guard against exception stack overflow; then:
// Top of normal user stack
#define USTACKTOP	(UXSTACKTOP - 2*PGSIZE)

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
typedef uint32_t pte_t;
typedef uint32_t pde_t;

extern volatile pte_t vpt[];     // VA of "virtual page table"
extern volatile pde_t vpd[];     // VA of current page directory

#endif /* !__ASSEMBLER__ */
#endif /* !ROS_INC_MEMLAYOUT_H */
