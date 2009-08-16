/* Copyright (c) 2009 The Regents of the University  of California. 
 * See the COPYRIGHT files at the top of this source tree for full 
 * license information.
 * 
 * Kevin Klues <klueska@cs.berkeley.edu>    
 */

#include <arch/types.h>
#include <arch/mmu.h>
#include <colored_caches.h>
#include <stdio.h>

/************** Cache Related Functions  *****************/
inline void init_cache_properties(cache_t *c, size_t sz_k, size_t wa, size_t clsz) {
	c->wa = wa;
	c->sz_k = sz_k;
	c->clsz = clsz;
	
	//Added as optimization (derived from above);
	c->num_colors = get_cache_num_page_colors(c);
}
inline size_t get_page_color(uintptr_t page, cache_t *c) {
    return (page % c->num_colors);
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
	return (LOG2(get_cache_line_size_bytes(c)));
}
inline size_t get_cache_num_index_bits(cache_t *c) {
	return (LOG2(get_cache_size_bytes(c) 
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