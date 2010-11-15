/* Copyright (c) 2009 The Regents of the University of California. 
 * See the COPYRIGHT files at the top of this source tree for full 
 * license information.
 * 
 * Kevin Klues <klueska@cs.berkeley.edu>    
 */

#ifndef ROS_SYSEVENT_H
#define ROS_SYSEVENT_H

#include <ros/ring_buffer.h>
#include <ros/arch/mmu.h>


typedef enum {
	SYS_begofevents, //Should always be first
	
	SYS_shared_page_alloc_event, 
	SYS_shared_page_free_event, 

	SYS_endofevents //Should always be last
} sysevent_type_t;

#define NUM_SYSEVENT_ARGS 6
typedef struct sysevent {
	sysevent_type_t type;
	uint32_t args[NUM_SYSEVENT_ARGS];
} sysevent_t;

typedef struct sysevent_rsp {
	int rsp;
} sysevent_rsp_t;

// Generic Sysevent Ring Buffer
#define SYSEVENTRINGSIZE    PGSIZE
DEFINE_RING_TYPES(sysevent, sysevent_t, sysevent_rsp_t);

#endif //ROS_SYSEVENT_H

