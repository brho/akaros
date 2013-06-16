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

/* TODO: sort out multiboot being in src/ (depends on this) */
#ifndef EXTPHYSMEM
#define EXTPHYSMEM	0x100000
#endif

#define KSTKSHIFT	(PGSHIFT)			/* KSTKSIZE == PGSIZE */
#define KSTKSIZE	(1 << KSTKSHIFT)	/* size of a static kernel stack */

/*
 * User read-only mappings! Anything below here til UWLIM are readonly to user.
 * They are global pages mapped in at env allocation time.
 */

// Same as VPT but read-only for users
#define UVPT		(ULIM - PTSIZE)

/*
 * Top of user VM. User can manipulate VA from UWLIM-1 and down!
 */

// Top of user-accessible VM
#define UWLIM		(UVPT - PTSIZE)
// Read-only, per-process shared info structures
#define UINFO		UWLIM

// Read-write, per-process shared page for sending asynchronous 
// syscalls to the kernel
#define UDATA    (UWLIM - PTSIZE)

// Read-write, global page.  Shared by all processes.  Can't be trusted.
#define UGDATA   (UDATA - PGSIZE)

// Top of one-page user exception stack
#define UXSTACKTOP	UGDATA
/* Limit of what is mmap()/munmap()-able */
#define UMAPTOP UXSTACKTOP
// Next page left invalid to guard against exception stack overflow; then:
// Top of normal user stack
#define USTACKTOP	(UXSTACKTOP - 2*PGSIZE)
// Maximum stack depth preallocated to 1MB
#define USTACK_NUM_PAGES	256
// Next page left invalid to guard against stack overflow
// Maximum bottom of normal user stack
#define USTACKBOT	(USTACKTOP - (USTACK_NUM_PAGES+1)*PGSIZE)

// Arbitrary boundary between the break and the start of
// memory returned by calls to mmap with addr = 0
#define BRK_END 0x40000000

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

extern volatile pte_t *vpt; // VA of "virtual page table"
extern volatile pde_t *vpd; // VA of current page directory

#endif /* !__ASSEMBLER__ */
#endif /* !ROS_INC_MEMLAYOUT_H */
