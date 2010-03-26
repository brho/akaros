/* Copyright (c) 2009 The Regents of the University  of California. 
 * See the COPYRIGHT files at the top of this source tree for full 
 * license information.
 * 
 * Kevin Klues <klueska@cs.berkeley.edu>    
 */
 
#ifndef ROS_KERN_COLORED_CACHES_H
#define ROS_KERN_COLORED_CACHES_H

#include <ros/common.h>
#include <error.h>
#include <atomic.h>

/****************** Cache Structures ********************/
typedef struct Cache {
	size_t wa;
	size_t sz_k;
	size_t clsz;
	uint8_t* free_colors_map;
	
	//Added as optimization (derived from above);
	size_t num_colors;
} cache_t;

typedef struct AvailableCaches {
	cache_t* l1;
	cache_t* l2;
	cache_t* l3;
} available_caches_t;

/******** Externally visible global variables ************/
extern available_caches_t RO available_caches;
extern cache_t* llc_cache;
extern spinlock_t cache_colors_lock;

/************** Cache Related Functions  *****************/
void cache_init();
void cache_color_alloc_init();
void init_cache_properties(cache_t RO*c, size_t sz_k, size_t wa, size_t clsz);
void init_free_cache_colors_map(cache_t* c);
size_t get_offset_in_cache_line(uintptr_t addr, cache_t RO*c);
void print_cache_properties(char *NT lstring, cache_t RO*c);

static inline size_t get_page_color(uintptr_t page, cache_t *c) {
    return (page & (c->num_colors-1));
}


uint8_t* cache_colors_map_alloc();
void cache_colors_map_free(uint8_t* colors_map);
error_t cache_color_alloc(cache_t* c, uint8_t* colors_map);
error_t cache_color_alloc_specific(size_t color, cache_t* c, 
                                         uint8_t* colors_map);
void cache_color_free(cache_t* c, uint8_t* colors_map);
void cache_color_free_specific(size_t color, cache_t* c, uint8_t* colors_map);

/****************** Cache Properties *********************/
inline size_t get_cache_ways_associative(cache_t RO*c);
inline size_t get_cache_line_size_bytes(cache_t RO*c);
inline size_t get_cache_size_bytes(cache_t RO*c);
inline size_t get_cache_size_kilobytes(cache_t RO*c);
inline size_t get_cache_size_megabytes(cache_t RO*c);
inline size_t get_cache_num_offset_bits(cache_t RO*c);
inline size_t get_cache_num_index_bits(cache_t RO*c);
inline size_t get_cache_num_tag_bits(cache_t RO*c);
inline size_t get_cache_num_page_color_bits(cache_t RO*c);
inline size_t get_cache_bytes_per_line(cache_t RO*c);
inline size_t get_cache_num_lines(cache_t RO*c);
inline size_t get_cache_num_sets(cache_t RO*c);
inline size_t get_cache_lines_per_set(cache_t RO*c);
inline size_t get_cache_lines_per_page(cache_t RO*c);
inline size_t get_cache_bytes_per_way(cache_t RO*c);
inline size_t get_cache_lines_per_way(cache_t RO*c);
inline size_t get_cache_pages_per_way(cache_t RO*c);
inline size_t get_cache_num_page_colors(cache_t RO*c);

#endif // ROS_KERN_COLORED_CACHES_H

