/* Copyright (c) 2009 The Regents of the University  of California.
 * See the COPYRIGHT files at the top of this source tree for full
 * license information.
 *
 * Kevin Klues <klueska@cs.berkeley.edu>
 */

#include <error.h>
#include <bitmask.h>
#include <colored_caches.h>
#include <process.h>

// Static global variable of caches to assign to the available caches struct
static cache_t l1,l2,l3;

// Convenient global variable for accessing the last level cache
cache_t* llc_cache;

// Global variables
available_caches_t available_caches;

/************** Cache Related Functions  *****************/
void cache_init()
{
	// Initialize the caches available on this system.
	// TODO: Should call out to something reading the acpi tables from
	// memory, or something similar.  For now, just initialize them inline
	available_caches.l1 = &l1;
	available_caches.l2 = &l2;
	available_caches.l3 = &l3;
	llc_cache = &l3;
#ifdef CONFIG_BOXBORO
	/* level (ignoring L1I), size, ways, CL size) */
	init_cache_properties(&l1,   32,  8, 64);	/* 1 color */
	init_cache_properties(&l2,  256,  8, 64);	/* 16 colors */
	init_cache_properties(&l3, 24576, 24, 64);	/* 256 colors */
#else /* Core i7 */
	init_cache_properties(&l1,   32,  8, 64);	/* 1 color */
	init_cache_properties(&l2,  256,  8, 64);	/* 16 colors */
	init_cache_properties(&l3, 8192, 16, 64);	/* 128 colors */
#endif /* CONFIG_E1000_ON_BOXBORO */
	printk("Cache init successful\n");
}

void cache_color_alloc_init()
{
	init_free_cache_colors_map(&l1);
	init_free_cache_colors_map(&l2);
	init_free_cache_colors_map(&l3);
}

