/*
 * Copyright (c) 2009 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Memory management flags, currently used in mmap()
 */

#ifndef ROS_INCLUDE_MMAN_H
#define ROS_INCLUDE_MMAN_H

/* Memory protection states (what you're allowed to do */
#define PROT_READ		0x4
#define PROT_WRITE		0x2
#define PROT_EXEC		0x1
#define PROT_NONE		0x0
#define PROT_UNMAP		0x100

/* mmap flags, only anonymous is supported now, feel free to pass others */
#define MAP_SHARED		0x010
#define MAP_PRIVATE		0x000
#define MAP_ANONYMOUS	0x002
#define MAP_FIXED		0x100
//#define MAP_GROWSDOWN	0x010
//#define MAP_STACK		0x020
//#define MAP_POPULATE	0x040
//#define MAP_NONBLOCK	0x080
//#define MAP_NORESERVE	0x100

/* Other mmap flags, which we probably won't support
#define MAP_32BIT
#define MAP_LOCKED
*/

#endif /* !ROS_INCLUDE_MMAN_H */
