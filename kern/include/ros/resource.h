/*
 * Copyright (c) 2009 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Interface for asking for resources from the kernel.
 */

#ifndef ROS_INCLUDE_RESOURCE_H
#define ROS_INCLUDE_RESOURCE_H

#include <ros/common.h>

/* A request means to set the amt_wanted to X.  Any changes result in prodding
 * the scheduler / whatever.
 *
 * To make these requests, userspace uses SYS_resource_req, which currently is a
 * syscall to make one request.
 *
 * Another way would be to take a ptr to a resource req and length, to batch
 * requests together.  Individual syscalls are simpler than the batch.  For
 * example,  servicing the core request doesn't easily return (which could lead
 * to other requests getting ignored, or us having to worry about the
 * order of processing).  Dealing with more than one request per type could be a
 * pain too.  The batch one is nice, since it amortizes the overhead of the syscall,
 * but it doesn't really matter that much, esp when there are only a few resources.
 *
 * amt_wanted_min is the least amount you are will to run with.
 *
 * A few caveats for cores:
 * - when someone yields (esp if the wish > grant): yielding means take one
 *   away, and set wished = current.  don't yield if you want another core still
 * - if someone requests less cores than they currently have active, we'll set
 *   their wish to their active and return an error code (no core allocation
 *   changes either).
 */

/* Types of resource requests */
#define RES_CORES			 0
#define RES_MEMORY			 1
#define RES_APPLE_PIES		 2
#define MAX_NUM_RESOURCES    3

/* Flags */
#define REQ_ASYNC			0x01 // Sync by default (?)
#define REQ_SOFT			0x02 // just making something up

struct resource {
	int type;
	size_t amt_wanted;
	size_t amt_wanted_min;
	size_t amt_granted;
	uint32_t flags;
};

#endif // !ROS_INCLUDE_RESOURCE_H
