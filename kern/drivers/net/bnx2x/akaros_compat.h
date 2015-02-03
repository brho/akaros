/* Copyright (c) 2015 Google Inc.
 *
 * Dumping ground for converting between Akaros and other OSs. */

#ifndef ROS_KERN_AKAROS_COMPAT_H
#define ROS_KERN_AKAROS_COMPAT_H

#define __rcu
typedef unsigned long dma_addr_t;

/* Common headers that most driver files will need */

#include <assert.h>
#include <error.h>
#include <ip.h>
#include <kmalloc.h>
#include <kref.h>
#include <pmap.h>
#include <slab.h>
#include <smp.h>
#include <stdio.h>
#include <string.h>
#include <bitmap.h>
#include <mii.h>
#include <umem.h>

#endif /* ROS_KERN_AKAROS_COMPAT_H */
