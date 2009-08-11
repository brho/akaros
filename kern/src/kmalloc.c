/* Copyright (c) 2009 The Regents of the University of California. 
 * See the COPYRIGHT files at the top of this source tree for full 
 * license information.
 * 
 * Kevin Klues <klueska@cs.berkeley.edu>    
 */

#include <arch/types.h>
#include <ros/error.h>
#include <pmap.h>
#include <kmalloc.h>
#include <stdio.h>

#define kmallocdebug(args...)  printk(args)

static page_list_t pages_list;	//List of physical pages used by kmalloc
extern size_t naddrpage;

void kmalloc_init() {
	LIST_INIT(&pages_list);
}

void* kmalloc(size_t size, int flags) {
	int npages = ROUNDUP(size, PGSIZE) / PGSIZE;
	
	// Find 'npages' free consecutive pages
	int first = -1;
	kmallocdebug("naddrpage: %u\n", naddrpage);
	kmallocdebug("npages: %u\n", npages);
	for(int i=(naddrpage-1); i>=(npages-1); i--) {
		int j;
		for(j=i; j>=i-(npages-1); j--) {
			if( !page_is_free(j) )
				break;
		}
		if( j == i-(npages-1)-1 ) {
			first = j+1;
			break;
		}
	}
	//If we couldn't find them, return NULL
	if( first == -1 )
		return NULL;
	
	//Otherwise go ahead and allocate them to ourselves now
	for(int i=0; i<npages; i++) {
		page_t* page;
		page_alloc_specific(&page, first+i);
		page->num_cons_links = npages-i;
		LIST_INSERT_HEAD(&pages_list, page, pp_link);
		kmallocdebug("mallocing page: %u\n", first+i);
		kmallocdebug("at addr: %p\n", ppn2kva(first+i));
	}
	//And return the address of the first one
	return ppn2kva(first);
}
void kfree(void *addr) {
	kmallocdebug("incoming address: %p\n", addr);
	page_t* page = kva2page(addr);
	int num_links = page->num_cons_links;
	kmallocdebug("getting page: %u\n", page2ppn(page));
	for(int i=0; i<num_links; i++) {
		page_t* p = ppn2page((page2ppn(page) + i));
		LIST_REMOVE(p, pp_link);
		page_free(p);
		kmallocdebug("freeing page: %d\n", page2ppn(p));
	}
}