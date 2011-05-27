/* Copyright (c) 2009 The Regents of the University  of California. 
 * See the COPYRIGHT files at the top of this source tree for full 
 * license information.
 * 
 * Kevin Klues <klueska@cs.berkeley.edu>    
 */

#include <colored_caches.h>
#include <stdio.h>

#ifdef __SHARC__
#pragma nosharc
#endif

// Global variables
static cache_t l1,l2,l3;
cache_t* llc_cache;
available_caches_t available_caches;

/************** Cache Related Functions  *****************/
void cache_init() 
{
	// Initialize the caches available on this system.
	// TODO: Should call out to something reading the acpi tables from 
	// memory, or something similar.  For now, just initialize them inline
	available_caches.l1 = SINIT(&l1);
	available_caches.l2 = SINIT(&l2);
	available_caches.l3 = SINIT(&l3);
	llc_cache = &l3;
	init_cache_properties(&l1,   32,  8, 64);
	init_cache_properties(&l2,  256,  8, 64);
	init_cache_properties(&l3, 8192, 16, 64);
	printk("Cache init successful\n");
}

void cache_color_alloc_init()
{
	init_free_cache_colors_map(&l1);
	init_free_cache_colors_map(&l2);
	init_free_cache_colors_map(&l3);
}

