/* Copyright (c) 2009 The Regents of the University of California. 
 * See the COPYRIGHT files at the top of this source tree for full 
 * license information.
 * 
 * Kevin Klues <klueska@cs.berkeley.edu>    
 */

#ifndef ROS_KERN_KMALLOC_H
#define ROS_KERN_KMALLOC_H

#include <ros/common.h>

#define NUM_KMALLOC_CACHES 13
#define KMALLOC_ALIGNMENT 16
#define KMALLOC_SMALLEST 32
#define KMALLOC_LARGEST KMALLOC_SMALLEST << NUM_KMALLOC_CACHES
#define KMALLOC_OFFSET ROUNDUP(sizeof(struct kmalloc_tag), KMALLOC_ALIGNMENT)

void* (DALLOC(n) boot_alloc)(uint32_t n, uint32_t align);
void* (DALLOC(n*sz) boot_calloc)(uint32_t n, size_t sz, uint32_t align);

void kmalloc_init(void);
void* (DALLOC(size) kmalloc)(size_t size, int flags);
void* (DALLOC(size) krealloc)(void* buf, size_t size, int flags);
void  (DFREE(addr) kfree)(void *addr);

/* Flags */
#define KMALLOC_TAG_CACHE 1
#define KMALLOC_TAG_PAGES 2

#define KMALLOC_CANARY 0xdeadbabe

struct kmalloc_tag {
	int flags;
	union {
		struct kmem_cache *my_cache WHEN(flags == KMALLOC_TAG_CACHE);
		size_t num_pages WHEN(flags == KMALLOC_TAG_PAGES);
	};
	uint32_t canary;
};

#endif //ROS_KERN_KMALLOC_H

