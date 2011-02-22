

#include <stdlib.h>
#include "linked_list.h"
#include "debug.h"

#ifndef DEBUG_linked_list_c
#undef debug
#define debug(...)
#undef tdebug
#define tdebug(...)
#endif


// the minimum allocation block.  
//
// FIXME: this should probably not be static, and probably should not
// be defined here.
#define MIN_ALLOC_BLOCK (1024)

// choose a macro for integrity checking
#ifdef DEBUG_linked_list_c
#define check_list_integrity(ll) {\
  assert(ll->num_entries >= 0);  \
  assert(ll->num_entries == 0  ? (ll->head == NULL  &&  ll->tail == NULL) : 1);  \
  assert(ll->num_entries == 1  ? (ll->head == ll->tail) : 1);  \
  assert(ll->num_entries  > 1  ? (ll->head != NULL  &&  ll->tail != NULL  &&  ll->head != ll->tail) : 1);  \
  { \
    int _num = 0; \
    linked_list_entry_t *e = ll->head; \
    assert( !e || !e->p.prev ); \
    while( e ) { \
      _num++; \
      assert( e->list == ll ); \
      if( e->p.next ) assert(e == (linked_list_entry_t*)e->p.next->prev); \
      e = (linked_list_entry_t*)e->p.next; \
    } \
    assert( _num == ll->num_entries ); \
  } \
}
#else
#define check_list_integrity(ll) {}
#endif

/**
 * return a list entry to the free list
 *
 * FIXME: perhaps garbage collect entries over time?
 **/
void ll_free_entry(linked_list_t *ll, void* e) 
{
  op_free_entry(ll->pool, (object_pool_entry_t*) e);
}


/**
 * Initialize an externally allocated linked list
 **/
void ll_init(linked_list_t *ll, const char *name, object_pool_t *pool)
{
  ll->name = (name ? name : "noname");
  ll->num_entries = 0;
  ll->head = NULL;
  ll->tail = NULL;
  ll->pool = pool;  
}


/**
 * Create a new linked list
 **/
linked_list_t* new_linked_list(const char *name, int entry_size)
{
  linked_list_t *ll;
  object_pool_t *pool;

  assert((unsigned int)entry_size > sizeof(linked_list_entry_t));

  ll = (linked_list_t*) malloc(sizeof(linked_list_t));    assert(ll);
  pool = new_object_pool(entry_size, MIN_ALLOC_BLOCK);    assert(pool);
  
  ll_init(ll, name, pool);
  
  check_list_integrity(ll);
  
  return ll;
}


/**
 * allocate & add a new entry to the end of the list.  The new entry
 * is returned, so the caller can fill in any required data.
 **/
linked_list_entry_t* ll_add_tail(linked_list_t *ll)
{
  linked_list_entry_t *e;
  
  check_list_integrity(ll);

  // allocate the new entry
  e = (linked_list_entry_t*) op_new_object(ll->pool);

  // add to the list
  ll_add_existing_to_tail(ll, e);

  check_list_integrity(ll);

  return e;
}

/**
 * allocate & add a new entry to the end of the list.  The new entry
 * is returned, so the caller can fill in any required data.
 **/
linked_list_entry_t* ll_add_head(linked_list_t *ll)
{
  linked_list_entry_t *e;
  
  check_list_integrity(ll);

  // allocate the new entry
  e = (linked_list_entry_t*) op_new_object(ll->pool);

  // add to the list
  ll_add_existing_to_head(ll, e);

  check_list_integrity(ll);

  return e;
}


linked_list_entry_t* ll_insert_before(linked_list_t *ll, linked_list_entry_t *item)
{
  linked_list_entry_t *e;
  
  check_list_integrity(ll);

  assert(item->list == ll);
  
  // allocate the new entry
  e = (linked_list_entry_t*) op_new_object(ll->pool);

  // add to the list
  ll_insert_existing_before(ll, item, e);

  check_list_integrity(ll);

  return e;
}

/**
 * add a pre-allocated item to the tail of the list
 **/
void ll_add_existing_to_tail(linked_list_t *ll, linked_list_entry_t *e)
{
  check_list_integrity(ll);

  // NOTE: it might be nice to check that e->list is not set, but this
  // means that externally allocated request entries have to be
  // specially initialized, which is error prone.
  //assert(e->list == NULL);
  e->list = ll;

  // queue is empty
  if(ll->head == NULL) {
    e->p.next = NULL;
    e->p.prev = NULL;
    ll->head = ll->tail = e;
  } 

  // add to the tail
  else {
    ll->tail->p.next = (object_pool_entry_t*) e;
    e->p.next = NULL;
    e->p.prev = (object_pool_entry_t*) ll->tail;
    ll->tail = e;
  }

  ll->num_entries++;

  check_list_integrity(ll);
}

/**
 * add a pre-allocated item to the tail of the list
 **/
void ll_add_existing_to_head(linked_list_t *ll, linked_list_entry_t *e)
{
  check_list_integrity(ll);

  // NOTE: it might be nice to check that e->list is not set, but this
  // means that externally allocated request entries have to be
  // specially initialized, which is error prone.
  //assert(e->list == NULL);
  e->list = ll;

  // queue is empty
  if(ll->head == NULL) {
    e->p.next = NULL;
    e->p.prev = NULL;
    ll->head = ll->tail = e;
  } 

  // add to the head
  else {
    e->p.prev = NULL;
    e->p.next = (object_pool_entry_t*) ll->head;
    ll->head->p.prev = (object_pool_entry_t*) e;
    ll->head = e;
  }

  ll->num_entries++;

  check_list_integrity(ll);
}

void ll_insert_existing_before(linked_list_t *ll, linked_list_entry_t *e, linked_list_entry_t *newe)
{
  check_list_integrity(ll);

  assert (e != NULL);
  
  // NOTE: it might be nice to check that newe->list is not set, but this
  // means that externally allocated request entries have to be
  // specially initialized, which is error prone.
  //assert(newe->list == NULL);
  newe->list = ll;

  newe->p.next = (object_pool_entry_t *) e;
  newe->p.prev = e->p.prev;
  
  if (e == ll->head) {
  // add to the head
    ll->head = newe;
  } else {
    ((linked_list_entry_t *)e->p.prev) ->p.next = (object_pool_entry_t *) newe;
  }

  e->p.prev = (object_pool_entry_t *) newe;

  ll->num_entries++;

  check_list_integrity(ll);
}


/**
 * remove the head of the list.  Note that the entry is NOT returned
 * to the free list: this must be done later by the caller.
 **/
linked_list_entry_t* ll_remove_head(linked_list_t *ll)
{
  linked_list_entry_t *e;

  check_list_integrity(ll);

  // the queue is empty
  if(ll->head == NULL) {
    return NULL;
  }

  // remove the head
  ll->num_entries--;
  e = ll->head;
  ll->head = (linked_list_entry_t*) e->p.next;
  if(ll->head == NULL) 
    ll->tail = NULL;
  else 
    ll->head->p.prev = NULL;

  check_list_integrity(ll);

  e->list = NULL;
  return e;
}

/**
 * remove the tail of the list.  Note that the entry is NOT returned
 * to the free list: this must be done later by the caller.
 **/
linked_list_entry_t* ll_remove_tail(linked_list_t *ll)
{
  linked_list_entry_t *e;

  check_list_integrity(ll);

  // the queue is empty
  if(ll->tail == NULL) {
    return NULL;
  }

  // remove the tail
  ll->num_entries--;
  e = ll->tail;
  ll->tail = (linked_list_entry_t*) e->p.prev;
  if(ll->tail == NULL) 
    ll->head = NULL;
  else
    ll->tail->p.next = NULL;

  check_list_integrity(ll);

  e->list = NULL;
  return e;
}

/**
 * remove the specified item from the list. Note that the entry is NOT returned
 * to the free list: this must be done later by the caller.
 **/
void ll_remove_entry(linked_list_t *ll, linked_list_entry_t *e)
{
  check_list_integrity(ll);

  // make sure the entry is actually on this list.  This allows
  // ll_remove_entry() to be safely called more than once on the same
  // item.
  if(e->list != ll) return;
  e->list = NULL;

  // fix up neighbors
  if(e->p.prev) e->p.prev->next = e->p.next;
  if(e->p.next) e->p.next->prev = e->p.prev;

  // fix head/tail pointers
  if(e->p.prev == NULL) ll->head = (linked_list_entry_t*) e->p.next;
  if(e->p.next == NULL) ll->tail = (linked_list_entry_t*) e->p.prev;

  // decrement the list size
  ll->num_entries--;

  check_list_integrity(ll);
}


// return the number of entries in the list
int ll_size(linked_list_t *ll)
{
  check_list_integrity(ll);
  return ll->num_entries;
}

/**
 * retrieve the first item in the list 
 **/
linked_list_entry_t* ll_view_head(linked_list_t *ll)
{
  check_list_integrity(ll);
  return ll->head;
}

/**
 * retrieve the last item in the list 
 **/
linked_list_entry_t* ll_view_tail(linked_list_t *ll)
{
  check_list_integrity(ll);
  return ll->tail;
}



/**
 * retrieve the next item in the list 
 **/
linked_list_entry_t* ll_view_next(linked_list_t *ll, linked_list_entry_t* e)
{
  check_list_integrity(ll);
  assert( e->list == ll );
#if OPTIMIZE >= 2
#ifndef DEBUG_linked_list_c
  (void) ll;
#endif
#endif
  return (linked_list_entry_t*) e->p.next;
}

/**
 * retrieve the prev item in the list 
 **/
linked_list_entry_t* ll_view_prev(linked_list_t *ll, linked_list_entry_t* e)
{
  check_list_integrity(ll);
  assert( e->list == ll );
#if OPTIMIZE >= 2
#ifndef DEBUG_linked_list_c
  (void) ll;
#endif
#endif
  return (linked_list_entry_t*) e->p.prev;
}



//////////////////////////////////////////////////////////////////////
//
//  Simplified wrappers, for the common case of a linked list of pointers
//
//////////////////////////////////////////////////////////////////////

typedef struct _pointer_list_entry {
  linked_list_entry_t e;
  const void *data;
} pointer_list_entry_t;


/**
 * create a new pointer list
 **/
pointer_list_t* new_pointer_list(const char *name)
{
  return new_linked_list(name,sizeof(pointer_list_entry_t));
}

/**
 * add a data item to the end of the list
 **/
void pl_add_tail(pointer_list_t *pl, const void *data)
{
  pointer_list_entry_t *e;

  e = (pointer_list_entry_t*) ll_add_tail(pl);
  e->data = data;
}

/**
 * add a data item to the front of the list
 **/
void pl_add_head(pointer_list_t *pl, const void *data)
{
  pointer_list_entry_t *e;

  e = (pointer_list_entry_t*) ll_add_head(pl);
  e->data = data;
}

/**
 * remove the head of the list, and return the associated data.  If
 * the list is empty, return -1, to distinguish between NULL data.
 **/
void* pl_remove_head(pointer_list_t *pl)
{
  pointer_list_entry_t *e;
  const void *data;
  
  e = (pointer_list_entry_t*) ll_remove_head(pl);
  if(e == NULL)
    return (void *) -1;

  data = e->data;
  ll_free_entry(pl,(linked_list_entry_t*)e);
  
  return (void*)data;
}

/**
 * remove a specific pointer
 */
void pl_remove_pointer(pointer_list_t *pl, const void *data)
{
  pointer_list_entry_t *e;
  
  e = (pointer_list_entry_t *) ll_view_head(pl);
  while (e && (e->data != data))
    e = (pointer_list_entry_t *) e->e.p.next;
  if (e)
    ll_remove_entry(pl, (linked_list_entry_t *)e);
}

void *pl_get_pointer(linked_list_entry_t *e)
{
  return (void*) ((pointer_list_entry_t *)e)->data;
}

void pl_set_pointer(linked_list_entry_t *e, const void *p)
{
  ((pointer_list_entry_t *)e)->data = p;
}

// return the number of entries in the list
int pl_size(pointer_list_t *pl)
{
  return pl->num_entries;
}

// view the head of the list
void* pl_view_head(pointer_list_t *pl)
{
  pointer_list_entry_t *e;
  
  e = (pointer_list_entry_t*) ll_view_head(pl);
  if(e == NULL) 
    return (void *) -1;

  return (void*)e->data;
}

