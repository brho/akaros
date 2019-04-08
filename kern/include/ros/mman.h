/*
 * Copyright (c) 2009 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Memory management flags, currently used in mmap().
 * Try to keep these in sync with /usr/include/bits/mman.h so we have less
 * issues with userspace.
 */

#pragma once

/* Memory protection states (what you're allowed to do */
#define PROT_READ		0x1
#define PROT_WRITE		0x2
#define PROT_EXEC		0x4
#define PROT_NONE		0x0

/* mmap flags, only anonymous is supported now, feel free to pass others */
#define MAP_SHARED		0x01
#define MAP_PRIVATE		0x02
#define MAP_FIXED		0x10
#define MAP_ANONYMOUS		0x20
#define MAP_ANON MAP_ANONYMOUS

#define MAP_LOCKED		0x02000
#define MAP_POPULATE		0x08000

#define MAP_FAILED		((void*)-1)
