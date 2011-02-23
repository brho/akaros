/* Copyright (c) 2009 The Regents of the University  of California. 
 * See the COPYRIGHT files at the top of this source tree for full 
 * license information.
 * 
 * Kevin Klues <klueska@cs.berkeley.edu>    
 */

#ifdef __SHARC__
#pragma nosharc
#define SINIT(x) x
#endif

#include <ros/common.h>
#include <arch/mmu.h>
#include <bitmask.h>
#include <colored_caches.h>
#include <stdio.h>
#include <atomic.h>
#include <kmalloc.h>
#include <page_alloc.h>

#define l1 (available_caches.l1)
#define l2 (available_caches.l2)
#define l3 (available_caches.l3)

spinlock_t cache_colors_lock;

/************** Cache Related Functions  *****************/
inline void init_cache_properties(cache_t *c, size_t sz_k, size_t wa, size_t clsz) {
	c->wa = SINIT(wa);
	c->sz_k = SINIT(sz_k);
	c->clsz = SINIT(clsz);

#ifdef __CONFIG_PAGE_COLORING__
	//Added as optimization (derived from above);
	size_t nc = get_cache_num_page_colors(c);
	c->num_colors = SINIT(nc);
#else
	c->num_colors = 1;
#endif
}

inline void init_free_cache_colors_map(cache_t* c) 
{
	// Initialize the free colors map
	c->free_colors_map = kmalloc(c->num_colors, 0);
	FILL_BITMASK(c->free_colors_map, c->num_colors);
}

inline size_t get_offset_in_cache_line(uintptr_t addr, cache_t *c) {
    return (addr % get_cache_bytes_per_line(c));
}

void print_cache_properties(char *NT lstring, cache_t *c)
{
	printk("%s_WAYS_ASSOCIATIVE: %ld\n", lstring, get_cache_ways_associative(c));
	printk("%s_LINE_SIZE_BYTES: %ld\n", lstring, get_cache_line_size_bytes(c));
	printk("%s_SIZE_BYTES: %ld\n", lstring, get_cache_size_bytes(c));
	printk("%s_SIZE_KILOBYTES: %ld\n", lstring, get_cache_size_kilobytes(c));
	printk("%s_SIZE_MEGABYTES: %ld\n", lstring, get_cache_size_megabytes(c));
	printk("%s_OFFSET_BITS: %ld\n", lstring, get_cache_num_offset_bits(c));
	printk("%s_INDEX_BITS: %ld\n", lstring, get_cache_num_index_bits(c));
	printk("%s_TAG_BITS: %ld\n", lstring, get_cache_num_tag_bits(c));
	printk("%s_PAGE_COLOR_BITS: %ld\n", lstring, get_cache_num_page_color_bits(c));
	printk("%s_BYTES_PER_LINE: %ld\n", lstring, get_cache_bytes_per_line(c));
	printk("%s_NUM_LINES: %ld\n", lstring, get_cache_num_lines(c));
	printk("%s_NUM_SETS: %ld\n", lstring, get_cache_num_sets(c));
	printk("%s_LINES_PER_SET: %ld\n", lstring, get_cache_lines_per_set(c));
	printk("%s_LINES_PER_PAGE: %ld\n", lstring, get_cache_lines_per_page(c));
	printk("%s_BYTES_PER_WAY: %ld\n", lstring, get_cache_bytes_per_way(c));
	printk("%s_LINES_PER_WAY: %ld\n", lstring, get_cache_lines_per_way(c));
	printk("%s_PAGES_PER_WAY: %ld\n", lstring, get_cache_pages_per_way(c));
	printk("%s_NUM_PAGE_COLORS: %ld\n", lstring, get_cache_num_page_colors(c));
}

/****************** Cache Properties *********************/
inline size_t get_cache_ways_associative(cache_t *c) {
	return (c->wa);
}
inline size_t get_cache_line_size_bytes(cache_t *c) {
	return (c->clsz);
}
inline size_t get_cache_size_bytes(cache_t *c) {
	return (c->sz_k * ONE_KILOBYTE);
}
inline size_t get_cache_size_kilobytes(cache_t *c) {
	return (c->sz_k);
}
inline size_t get_cache_size_megabytes(cache_t *c) {
	return (c->sz_k / ONE_KILOBYTE);
}
inline size_t get_cache_num_offset_bits(cache_t *c) {
	return (LOG2_UP(get_cache_line_size_bytes(c)));
}
inline size_t get_cache_num_index_bits(cache_t *c) {
	return (LOG2_UP(get_cache_size_bytes(c) 
                   / get_cache_ways_associative(c)
                   / get_cache_line_size_bytes(c)));
}
inline size_t get_cache_num_tag_bits(cache_t *c) {
	return (NUM_ADDR_BITS - get_cache_num_offset_bits(c) 
                          - get_cache_num_index_bits(c));
}
inline size_t get_cache_num_page_color_bits(cache_t *c) {
	return (get_cache_num_offset_bits(c) 
                  + get_cache_num_index_bits(c) 
                  - PGSHIFT); 
}
inline size_t get_cache_bytes_per_line(cache_t *c) {
	return (1 << get_cache_num_offset_bits(c));
}
inline size_t get_cache_num_lines(cache_t *c) {
	return (get_cache_size_bytes(c)/get_cache_bytes_per_line(c));  
}
inline size_t get_cache_num_sets(cache_t *c) {
	return (get_cache_num_lines(c)/get_cache_ways_associative(c));
}
inline size_t get_cache_lines_per_set(cache_t *c) {
	return (get_cache_ways_associative(c));
}
inline size_t get_cache_lines_per_page(cache_t *c) {
	return (PGSIZE / get_cache_bytes_per_line(c));
}
inline size_t get_cache_bytes_per_way(cache_t *c) {
	return (get_cache_size_bytes(c)/get_cache_ways_associative(c));
} 
inline size_t get_cache_lines_per_way(cache_t *c) {
	return (get_cache_num_lines(c)/get_cache_ways_associative(c));
} 
inline size_t get_cache_pages_per_way(cache_t *c) {
	return (get_cache_lines_per_way(c)/get_cache_lines_per_page(c));
}
inline size_t get_cache_num_page_colors(cache_t *c) {
	return get_cache_pages_per_way(c);
}

static inline void set_color_range(uint16_t color, uint8_t* map, 
                                   cache_t* smaller, cache_t* bigger)
{
	size_t base, r;
	if(smaller->num_colors <= bigger->num_colors) {
		r = bigger->num_colors / smaller->num_colors;
		base = color*r;
		SET_BITMASK_RANGE(map, base, base+r);
	}
	else {
		r = smaller->num_colors / bigger->num_colors;
		base = color/r;
		if(BITMASK_IS_SET_IN_RANGE(smaller->free_colors_map, 
		                           base*r, base*r+r-1))
			SET_BITMASK_BIT(map, base);
	}
}

static inline void clr_color_range(uint16_t color, uint8_t* map, 
                                   cache_t* smaller, cache_t* bigger)
{
	size_t base, r;
	if(smaller->num_colors <= bigger->num_colors) {
		r = bigger->num_colors / smaller->num_colors;
		base = color*r;
		CLR_BITMASK_RANGE(map, base, base+r);
	}
	else {
		r = smaller->num_colors / bigger->num_colors;
		base = color/r;
		CLR_BITMASK_BIT(map, base);
	}
}

static inline error_t __cache_color_alloc_specific(size_t color, cache_t* c, 
                                                         uint8_t* colors_map) 
{
	if(!GET_BITMASK_BIT(c->free_colors_map, color))
		return -ENOCACHE;	
	
	if(l1)
		clr_color_range(color, l1->free_colors_map, c, l1);
	if(l2)
		clr_color_range(color, l2->free_colors_map, c, l2);
	if(l3)
		clr_color_range(color, l3->free_colors_map, c, l3);

	set_color_range(color, colors_map, c, llc_cache);
	return ESUCCESS;
}

static inline error_t __cache_color_alloc(cache_t* c, uint8_t* colors_map) 
{
	if(BITMASK_IS_CLEAR(c->free_colors_map, c->num_colors))
		return -ENOCACHE;	

	int color=0;
	do {
		if(GET_BITMASK_BIT(c->free_colors_map, color))
			break;
	} while(++color);

	return __cache_color_alloc_specific(color, c, colors_map);	
}

static inline void __cache_color_free_specific(size_t color, cache_t* c, 
                                                     uint8_t* colors_map) 
{
	if(GET_BITMASK_BIT(c->free_colors_map, color))
		return;
	else {
		size_t r = llc_cache->num_colors / c->num_colors;
		size_t base = color*r;
		if(!BITMASK_IS_SET_IN_RANGE(colors_map, base, base+r))
			return;
	}

	if(l3)
		set_color_range(color, l3->free_colors_map, c, l3);
	if(l2)
		set_color_range(color, l2->free_colors_map, c, l2);
	if(l1)
		set_color_range(color, l1->free_colors_map, c, l1);

	clr_color_range(color, colors_map, c, llc_cache);
}

static inline void __cache_color_free(cache_t* c, uint8_t* colors_map) 
{
	if(BITMASK_IS_FULL(c->free_colors_map, c->num_colors))
		return;	

	int color=0;
	do {
		if(!GET_BITMASK_BIT(c->free_colors_map, color)) {
			size_t r = llc_cache->num_colors / c->num_colors;
			size_t base = color*r;
			if(BITMASK_IS_SET_IN_RANGE(colors_map, base, base+r))
				break;
		}
	} while(++color < c->num_colors);
	if(color == c->num_colors)
		return;

	__cache_color_free_specific(color, c, colors_map);	
}

uint8_t* cache_colors_map_alloc() {
#ifdef __CONFIG_PAGE_COLORING__
	uint8_t* colors_map = kmalloc(llc_cache->num_colors, 0);
	if(colors_map)
		CLR_BITMASK(colors_map, llc_cache->num_colors);
	return colors_map;
#else
	return global_cache_colors_map;
#endif
}

void cache_colors_map_free(uint8_t* colors_map) {
#ifdef __CONFIG_PAGE_COLORING__
	kfree(colors_map);
#endif
}

error_t cache_color_alloc(cache_t* c, uint8_t* colors_map) 
{
	spin_lock_irqsave(&cache_colors_lock);
	error_t e = __cache_color_alloc(c, colors_map);
	spin_unlock_irqsave(&cache_colors_lock);
	return e;
}
error_t cache_color_alloc_specific(size_t color, cache_t* c, uint8_t* colors_map) 
{
	spin_lock_irqsave(&cache_colors_lock);
	error_t e = __cache_color_alloc_specific(color, c, colors_map);
	spin_unlock_irqsave(&cache_colors_lock);
	return e;
}

void cache_color_free(cache_t* c, uint8_t* colors_map) 
{
	spin_lock_irqsave(&cache_colors_lock);
	__cache_color_free(c, colors_map);
	spin_unlock_irqsave(&cache_colors_lock);
}
void cache_color_free_specific(size_t color, cache_t* c, uint8_t* colors_map) 
{
	spin_lock_irqsave(&cache_colors_lock);
	__cache_color_free_specific(color, c, colors_map);
	spin_unlock_irqsave(&cache_colors_lock);
}

