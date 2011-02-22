

#ifndef LINKED_LIST_H
#define LINKED_LIST_H

#include "object_pool.h"

// list entry structure.  users of this class should include this as 
//typedef object_pool_entry_t linked_list_entry_t;
struct _linked_list;
typedef struct _linked_list_entry {
  object_pool_entry_t p;
  struct _linked_list *list;
} linked_list_entry_t;

// a linked list, with built-in management of variable-sized entries
typedef struct _linked_list {
  const char *name;
  int num_entries;

  linked_list_entry_t *head;
  linked_list_entry_t *tail;

  // manage free list entries
  object_pool_t *pool;
} linked_list_t;


// return a list entry to the free list
void ll_free_entry(linked_list_t *ll, void* e);

// Initialize an externally allocated linked list
void ll_init(linked_list_t *ll, const char *name, object_pool_t *pool);

// Create a new linked list
linked_list_t* new_linked_list(const char *name, int entry_size);


// allocate & add a new entry to the end of the list.  The new entry
// is returned, so the caller can fill in any required data.
linked_list_entry_t* ll_add_tail(linked_list_t *ll);
linked_list_entry_t* ll_add_head(linked_list_t *ll);
linked_list_entry_t* ll_insert_before(linked_list_t *ll, linked_list_entry_t *item);

// add a pre-allocated entry to the end of the list.
void ll_add_existing_to_tail(linked_list_t *ll, linked_list_entry_t *item);
void ll_add_existing_to_head(linked_list_t *ll, linked_list_entry_t *item);
void ll_insert_existing_before(linked_list_t *ll, linked_list_entry_t *item, linked_list_entry_t *newitem);

// remove the head of the list.  Note that the entry is NOT returned
// to the free list: this must be done later by the caller.
linked_list_entry_t* ll_remove_head(linked_list_t *ll);

// remove the tail of the list.  Note that the entry is NOT returned
// to the free list: this must be done later by the caller.
linked_list_entry_t* ll_remove_tail(linked_list_t *ll);

// remove the specified item from the list
void ll_remove_entry(linked_list_t *ll, linked_list_entry_t *e);

// return the number of entries in the list
int ll_size(linked_list_t *ll);

// retrieve the first item in the list 
linked_list_entry_t* ll_view_head(linked_list_t *ll);

// retrieve the last item in the list 
linked_list_entry_t* ll_view_tail(linked_list_t *ll);

// retrieve the next item in the list 
linked_list_entry_t* ll_view_next(linked_list_t *ll, linked_list_entry_t* e);

// retrieve the previous item in the list 
linked_list_entry_t* ll_view_prev(linked_list_t *ll, linked_list_entry_t* e);



//////////////////////////////////////////////////////////////////////
// a simple linked list, with pointer data
//////////////////////////////////////////////////////////////////////
typedef linked_list_t pointer_list_t;

// create a new pointer list
pointer_list_t* new_pointer_list(const char *name);

void pl_add_tail(pointer_list_t *pl, const void *data);
void pl_add_head(pointer_list_t *pl, const void *data);

void* pl_remove_head(pointer_list_t *pl);
void* pl_view_head(pointer_list_t *pl);

void pl_remove_pointer(pointer_list_t *pl, const void *data);

void *pl_get_pointer(linked_list_entry_t *e);
void pl_set_pointer(linked_list_entry_t *e, const void *p);

// return the number of entries in the list
int pl_size(pointer_list_t *pl);



#endif /* LINKED_LIST_H */

