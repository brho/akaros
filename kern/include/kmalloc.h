/* Copyright (c) 2009 The Regents of the University of California. 
 * See the COPYRIGHT files at the top of this source tree for full 
 * license information.
 * 
 * Kevin Klues <klueska@cs.berkeley.edu>    
 */

#ifndef ROS_KERN_KMALLOC_H
#define ROS_KERN_KMALLOC_H

#include <ros/common.h>

void  kmalloc_init();

void* (DALLOC(n) boot_alloc)(uint32_t n, uint32_t align);
void* (DALLOC(_n*sz) boot_calloc)(uint32_t _n, size_t sz, uint32_t align);

void* (DALLOC(size) kmalloc)(size_t size, int flags);
void  (DFREE(addr) kfree)(void *addr);

#endif //ROS_KERN_KMALLOC_H

