/*
 * Copyright (c) 2009 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Interface for asking for resources from the kernel.
 */

#pragma once

#include <ros/common.h>

/* Types of resource requests */
#define RES_CORES		0
#define RES_MEMORY		1
#define RES_APPLE_PIES		2
#define MAX_NUM_RESOURCES	3

/* Flags */
#define REQ_ASYNC		0x01 // Sync by default (?)
#define REQ_SOFT		0x02 // just making something up

struct resource_req {
	unsigned long			amt_wanted;
	unsigned long			amt_wanted_min;
	int				flags;
};
