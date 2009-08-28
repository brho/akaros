/* Copyright (c) 2009 The Regents of the University  of California. 
 * See the COPYRIGHT files at the top of this source tree for full 
 * license information.
 * 
 * Kevin Klues <klueska@cs.berkeley.edu>    
 */

#include <colored_caches.h>

// Global variables
cache_t l1,l2,l3;
available_caches_t available_caches;

/************** Cache Related Functions  *****************/
void cache_init() 
{
	// Initialize the caches available on this system.
	// TODO: Should call out to something reading the acpi tables from 
	// memory, or something similar.  For now, just initialize them inline
	init_cache_properties(&l1,   32,  8, 64);
	init_cache_properties(&l2,  256,  8, 64);
	init_cache_properties(&l3, 8192, 16, 64);
	available_caches.l1 = TRUE;
	available_caches.l2 = FALSE;
	available_caches.l3 = FALSE;
}
