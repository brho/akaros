/*
 * Copyright (c) 2009 The Regents of the University  of California.  
 * See the COPYRIGHT files at the top of this source tree for full 
 * license information.
 */
/**
 * @author Kevin Klues <klueska@cs.berkeley.edu>
 */
 
#ifndef ROS_KERN_COLORED_PAGE_ALLOC_H
#define ROS_KERN_COLORED_PAGE_ALLOC_H

#include <colored_caches.h>
#include <arch/colored_page_alloc.h>
#include <stdio.h>
	
#define DECLARE_CACHE_COLORED_PAGE_LINK(_cache)                               \
	page_list_entry_t _cache##_cache_colored_page_link;
	
#define DECLARE_CACHE_COLORED_PAGE_FREE_LIST(_cache)                          \
	page_list_t *COUNT(npages) _cache##_cache_colored_page_list = NULL;
	
#define DECLARE_EXTERN_CACHE_COLORED_PAGE_FREE_LIST(_cache)                   \
	extern page_list_t *COUNT(npages) _cache##_cache_colored_page_list;
	
#define DECLARE_CACHE_COLORED_PAGE_ALLOC_FUNCTION(_cache)                     \
error_t _cache##_page_alloc(page_t** page, size_t color)                      \
{                                                                             \
	/* 	TODO: Put a lock around this */                                       \
	if(!LIST_EMPTY(&(_cache##_cache_colored_page_list)[(color)])) {           \
		*(page) = LIST_FIRST(&(_cache##_cache_colored_page_list)[(color)]);   \
		LIST_REMOVE(*page, global_link);                                      \
		REMOVE_CACHE_COLORING_PAGE_FROM_FREE_LISTS(page);                     \
		page_clear(*page);                                                    \
		return ESUCCESS;                                                      \
	}                                                                         \
	return -ENOMEM;                                                           \
}

#define INIT_CACHE_COLORED_PAGE_FREE_LIST(_cache)                             \
{                                                                             \
	if(available_caches._cache == TRUE) {                                     \
	    uint8_t num_colors = get_cache_num_page_colors(&(_cache));            \
	    size_t list_size = num_colors*sizeof(page_list_t);                    \
	    _cache##_cache_colored_page_list                                      \
	       = (page_list_t*) boot_alloc(list_size, PGSIZE);                    \
		for(int i=0; i<num_colors; i++) {                                     \
			LIST_INIT(&(_cache##_cache_colored_page_list[i]));                \
		}                                                                     \
	}                                                                         \
}

#define REMOVE_CACHE_COLORING_PAGE_FROM_FREE_LIST(_page, _cache)              \
	if(available_caches._cache == TRUE)                                       \
		LIST_REMOVE(*(_page), _cache##_cache_colored_page_link);


#define INSERT_CACHE_COLORING_PAGE_ONTO_FREE_LIST(_page, _cache)              \
	if(available_caches._cache == TRUE) {                                     \
		LIST_INSERT_HEAD(                                                     \
		   &(_cache##_cache_colored_page_list                                 \
		         [get_page_color(page2ppn((_page)), &(_cache))]),             \
		   (_page),                                                           \
		   _cache##_cache_colored_page_link                                   \
		);                                                                    \
	}

#endif // ROS_KERN_COLORED_PAGE_ALLOC_H
