

#ifndef OBJECT_POOL_H
#define OBJECT_POOL_H

typedef struct _object_pool object_pool_t;

typedef struct _object_pool_entry {
  struct _object_pool_entry *next;
  struct _object_pool_entry *prev;
  // user data goes here...
} object_pool_entry_t;


// initialize an object pool
object_pool_t* new_object_pool(unsigned int entry_size, int min_alloc_block);

// get/allocate a new object
object_pool_entry_t* op_new_object(object_pool_t *pool);

// return an object to the free list
void op_free_entry(object_pool_t *pool, object_pool_entry_t* e);

#endif /* OBJECT_POOL_H */
