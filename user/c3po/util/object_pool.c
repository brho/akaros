
#include <stdlib.h>
#include "debug.h"
#include "object_pool.h"

#ifndef DEBUG_object_pool_c
#undef debug
#define debug(...)
#undef tdebug
#define tdebug(...)
#endif


struct _object_pool {

  // manage free list entries
  unsigned int entry_size;
  int malloc_size;
  object_pool_entry_t *free_entries;
  int num_free_entries;
  void *malloced_entries;
  int num_malloced_entries;
};


// initialize an object pool
object_pool_t* new_object_pool(unsigned int entry_size, int min_alloc_block)
{
  object_pool_t *pool = malloc(sizeof(object_pool_t));
  assert(pool);

  pool->num_free_entries = 0;
  pool->num_malloced_entries = 0;

  // sanity check entry_size
  assert(entry_size > sizeof(object_pool_entry_t));
  pool->entry_size = entry_size;

  // allocate in MIN_ALLOC_BLOCK-sized chunks, and no less than 100 entries at a time
  pool->malloc_size = 100 * entry_size;
  if(pool->malloc_size <= min_alloc_block)
    pool->malloc_size = min_alloc_block;
  else 
    pool->malloc_size = (pool->malloc_size / (min_alloc_block) + 1) * (min_alloc_block);

  return pool;
}



// get/allocate a new object
object_pool_entry_t* op_new_object(object_pool_t *pool)
{
  object_pool_entry_t *ne;

  // take from the free list
  if(pool->num_free_entries > 0) {
    ne = pool->free_entries;
    pool->free_entries = ne->next;
    pool->num_free_entries--;
    return ne;
  }

  // allocate more entries, if necessary
  if(pool->num_malloced_entries == 0) {
    pool->malloced_entries = malloc(pool->malloc_size);
    assert(pool->malloced_entries);
    pool->num_malloced_entries = (pool->malloc_size / pool->entry_size);
  }

  // take the next one from the malloced region
  ne = (object_pool_entry_t*) pool->malloced_entries;
  pool->malloced_entries += pool->entry_size;
  pool->num_malloced_entries--;
  return ne;
}


/**
 * return an object to the free list
 *
 * FIXME: perhaps garbage collect entries over time?  
 *
 * FIXME: bad things happen if someone frees an object that isn't from
 * this pool.  could include a pool ID if we get paranoid about this.
 **/
void op_free_entry(object_pool_t *pool, object_pool_entry_t* e) 
{
  e->next = pool->free_entries;
  pool->free_entries = e;
  pool->num_free_entries++;
}
