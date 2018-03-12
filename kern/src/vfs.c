/* Copyright (c) 2009, 2010 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Default implementations and global values for the VFS. */

#include <ros/errno.h>
#include <sys/queue.h>
#include <assert.h>
#include <stdio.h>
#include <atomic.h>
#include <slab.h>
#include <kmalloc.h>
#include <pmap.h>
#include <umem.h>
#include <smp.h>
#include <ns.h>
#include <fdtap.h>

