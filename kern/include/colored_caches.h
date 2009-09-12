/* Copyright (c) 2009 The Regents of the University  of California. 
 * See the COPYRIGHT files at the top of this source tree for full 
 * license information.
 * 
 * Kevin Klues <klueska@cs.berkeley.edu>    
 */
 
#ifndef ROS_KERN_COLORED_CACHES_H
#define ROS_KERN_COLORED_CACHES_H

#include <ros/common.h>

/****************** Cache Structures ********************/
typedef struct Cache {
	size_t wa;
	size_t sz_k;
	size_t clsz;
	
	//Added as optimization (derived from above);
	size_t num_colors;
} cache_t;

typedef struct AvailableCaches {
	uint8_t l1 : 1;
	uint8_t l2 : 1;
	uint8_t l3 : 1;

	// Pointer to the last level cache
	cache_t*   llc;
} available_caches_t;

/******** Externally visible global variables ************/
extern cache_t l1,l2,l3;
extern available_caches_t available_caches;

/************** Cache Related Functions  *****************/
void cache_init();
void init_cache_properties(cache_t *c, size_t sz_k, size_t wa, size_t clsz);
size_t get_page_color(uintptr_t page, cache_t *c);
size_t get_offset_in_cache_line(uintptr_t addr, cache_t *c);
void print_cache_properties(char *NT lstring, cache_t *c);

/****************** Cache Properties *********************/
inline size_t get_cache_ways_associative(cache_t *c);
inline size_t get_cache_line_size_bytes(cache_t *c);
inline size_t get_cache_size_bytes(cache_t *c);
inline size_t get_cache_size_kilobytes(cache_t *c);
inline size_t get_cache_size_megabytes(cache_t *c);
inline size_t get_cache_num_offset_bits(cache_t *c);
inline size_t get_cache_num_index_bits(cache_t *c);
inline size_t get_cache_num_tag_bits(cache_t *c);
inline size_t get_cache_num_page_color_bits(cache_t *c);
inline size_t get_cache_bytes_per_line(cache_t *c);
inline size_t get_cache_num_lines(cache_t *c);
inline size_t get_cache_num_sets(cache_t *c);
inline size_t get_cache_lines_per_set(cache_t *c);
inline size_t get_cache_lines_per_page(cache_t *c);
inline size_t get_cache_bytes_per_way(cache_t *c);
inline size_t get_cache_lines_per_way(cache_t *c);
inline size_t get_cache_pages_per_way(cache_t *c);
inline size_t get_cache_num_page_colors(cache_t *c);

#endif // ROS_KERN_COLORED_CACHES_H

