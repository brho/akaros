/*
 * Copyright (c) 2009 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Memory management flags, currently used in mmap().
 * Try to keep these in sync with /usr/include/bits/mman.h so we have less
 * issues with userspace.
 */

#ifndef ROS_INCLUDE_MMAN_H
#define ROS_INCLUDE_MMAN_H

/* Memory protection states (what you're allowed to do */
#define PROT_READ		0x1
#define PROT_WRITE		0x2
#define PROT_EXEC		0x4
#define PROT_NONE		0x0
#define PROT_GROWSDOWN	0x01000000
#define PROT_GROWSUP	0x02000000
// TODO NOT A REAL STATE
#define PROT_UNMAP		0x100

/* mmap flags, only anonymous is supported now, feel free to pass others */
#define MAP_SHARED		0x01
#define MAP_PRIVATE		0x02
#define MAP_FIXED		0x10
#define MAP_ANONYMOUS	0x20
#define MAP_ANON MAP_ANONYMOUS

#define MAP_GROWSDOWN	0x00100
#define MAP_DENYWRITE	0x00800
#define MAP_EXECUTABLE	0x01000
#define MAP_LOCKED		0x02000
#define MAP_NORESERVE	0x04000
#define MAP_POPULATE	0x08000
#define MAP_NONBLOCK	0x10000
#define MAP_STACK		0x20000

#define MAP_FAILED		((void*)-1)

/* Other mmap flags, which we probably won't support
#define MAP_32BIT
*/

#endif /* !ROS_INCLUDE_MMAN_H */
