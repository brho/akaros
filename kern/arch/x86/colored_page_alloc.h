/*
 * Copyright (c) 2009 The Regents of the University  of California.  
 * See the COPYRIGHT files at the top of this source tree for full 
 * license information.
 */
/**
 * @author Kevin Klues <klueska@cs.berkeley.edu>
 */
 
#ifndef ROS_KERN_ARCH_COLORED_PAGE_ALLOC_H
#define ROS_KERN_ARCH_COLORED_PAGE_ALLOC_H

/********** Page Coloring Related Macros ************/
// Define these to make sure that each level of the cache
// is initialized and managed properly
#define DECLARE_CACHE_COLORED_PAGE_LINKS()                    \
	DECLARE_CACHE_COLORED_PAGE_LINK(l1)                       \
	DECLARE_CACHE_COLORED_PAGE_LINK(l2)                       \
	DECLARE_CACHE_COLORED_PAGE_LINK(l3)

#define DECLARE_CACHE_COLORED_PAGE_FREE_LISTS()               \
	DECLARE_CACHE_COLORED_PAGE_FREE_LIST(l1)                  \
	DECLARE_CACHE_COLORED_PAGE_FREE_LIST(l2)                  \
	DECLARE_CACHE_COLORED_PAGE_FREE_LIST(l3)
	
#define DECLARE_EXTERN_CACHE_COLORED_PAGE_FREE_LISTS()        \
	DECLARE_EXTERN_CACHE_COLORED_PAGE_FREE_LIST(l1)           \
	DECLARE_EXTERN_CACHE_COLORED_PAGE_FREE_LIST(l2)           \
	DECLARE_EXTERN_CACHE_COLORED_PAGE_FREE_LIST(l3)
	
#define DECLARE_CACHE_COLORED_PAGE_ALLOC_FUNCTIONS()          \
	DECLARE_CACHE_COLORED_PAGE_ALLOC_FUNCTION(l1)             \
	DECLARE_CACHE_COLORED_PAGE_ALLOC_FUNCTION(l2)             \
	DECLARE_CACHE_COLORED_PAGE_ALLOC_FUNCTION(l3)

#define INIT_CACHE_COLORED_PAGE_FREE_LISTS()                  \
	INIT_CACHE_COLORED_PAGE_FREE_LIST(l1)                     \
	INIT_CACHE_COLORED_PAGE_FREE_LIST(l2)                     \
	INIT_CACHE_COLORED_PAGE_FREE_LIST(l3)

#define REMOVE_CACHE_COLORING_PAGE_FROM_FREE_LISTS(page)      \
	REMOVE_CACHE_COLORING_PAGE_FROM_FREE_LIST(page, l1)       \
	REMOVE_CACHE_COLORING_PAGE_FROM_FREE_LIST(page, l2)       \
	REMOVE_CACHE_COLORING_PAGE_FROM_FREE_LIST(page, l3)
	
#define INSERT_CACHE_COLORING_PAGE_ONTO_FREE_LISTS(page)      \
	INSERT_CACHE_COLORING_PAGE_ONTO_FREE_LIST(page, l1)       \
	INSERT_CACHE_COLORING_PAGE_ONTO_FREE_LIST(page, l2)       \
	INSERT_CACHE_COLORING_PAGE_ONTO_FREE_LIST(page, l3)

#endif // CACHE_COLORING_PAGE_ALLOC_H
