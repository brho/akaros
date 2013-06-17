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

/* Read-only, per-process shared info structures */
#define UINFO			(UVPT - PTSIZE)
/* Top of user-writable VM */
#define UWLIM			UINFO
/* Read-write, per-process shared info structures */
#define UDATA			(UWLIM - PTSIZE)
/* Read-write, global page.  Shared by all processes. */
#define UGDATA			(UDATA - PGSIZE)
/* Limit of what is mmap()/munmap()-able */
#define UMAPTOP			UGDATA
/* Top of normal user stack */
#define USTACKTOP		UMAPTOP
/* Stack size of thread0, allocated by the kernel */
#define USTACK_NUM_PAGES	256

#endif /* !ROS_INC_MEMLAYOUT_H */
