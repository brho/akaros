/*
 * Copyright (c) 2012 The Regents of the University of California
 * Kevin Klues <klueska@cs.berkeley.edu>
 *
 * This file is part of Parlib.
 *
 * Parlib is free software: you can redistribute it and/or modify
 * it under the terms of the Lesser GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * Parlib is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * Lesser GNU General Public License for more details.
 * 
 * See COPYING.LESSER for details on the GNU Lesser General Public License.
 * See COPYING for details on the GNU General Public License.
 */

#include <stddef.h>
#include "spinlock.h"
#include "dtls.h"
#include "slab.h"

/* The current dymamic tls implementation uses a locked linked list
 * to find the key for a given thread. We should probably find a better way to
 * do this based on a custom lock-free hash table or something. */
#include <sys/queue.h>
#include "spinlock.h"

/* The dynamic tls key structure */
struct dtls_key {
  atomic_t lock;
  int ref_count;
  bool valid;
  void (*dtor)(void*);
};

/* The definition of a dtls_key list and its elements */
struct dtls_value {
  TAILQ_ENTRY(dtls_value) link;
  struct dtls_key *key;
  void *dtls;
}; 
TAILQ_HEAD(dtls_list, dtls_value);

/* A struct containing all of the per thread (i.e. vcore or uthread) data
 * associated with dtls */
typedef struct dtls_data {
  /* A per-thread list of dtls regions */
  struct dtls_list list;

} dtls_data_t;

/* A slab of dtls keys (global to all threads) */
static struct kmem_cache *__dtls_keys_cache;

/* A slab of values for use when mapping a dtls_key to its per-thread value */
struct kmem_cache *__dtls_values_cache;
  
/* A lock protecting access to the caches above */
static atomic_t __dtls_lock;

static __thread dtls_data_t __dtls_data;
static __thread bool __dtls_initialized = false;

static dtls_key_t __allocate_dtls_key() 
{
  spinlock_lock(&__dtls_lock);
  dtls_key_t key = kmem_cache_alloc(__dtls_keys_cache, 0);
  assert(key);
  key->ref_count = 1;
  spinlock_unlock(&__dtls_lock);
  return key;
}

static void __maybe_free_dtls_key(dtls_key_t key)
{
  if(key->ref_count == 0) {
    spinlock_lock(&__dtls_lock);
    kmem_cache_free(__dtls_keys_cache, key);
    spinlock_unlock(&__dtls_lock);
  }
}

/* Constructor to get a reference to the main thread's TLS descriptor */
int dtls_lib_init()
{
  /* Make sure this only runs once */
  static bool initialized = false;
  if (initialized)
      return 0;
  initialized = true;
  
  /* Initialize the global cache of dtls_keys */
  __dtls_keys_cache = kmem_cache_create("dtls_keys_cache", 
    sizeof(struct dtls_key), __alignof__(struct dtls_key), 0, NULL, NULL);
  
  __dtls_values_cache = kmem_cache_create("dtls_values_cache", 
    sizeof(struct dtls_value), __alignof__(struct dtls_value), 0, NULL, NULL);
  
  /* Initialize the lock that protects the cache */
  spinlock_init(&__dtls_lock);
  return 0;
}

dtls_key_t dtls_key_create(dtls_dtor_t dtor)
{
  dtls_lib_init();
  dtls_key_t key = __allocate_dtls_key();
  spinlock_init(&key->lock);
  key->valid = true;
  key->dtor = dtor;
  return key;
}

void dtls_key_delete(dtls_key_t key)
{
  assert(key);

  spinlock_lock(&key->lock);
  key->valid = false;
  key->ref_count--;
  spinlock_unlock(&key->lock);
  __maybe_free_dtls_key(key);
}

static inline void __set_dtls(dtls_data_t *dtls_data, dtls_key_t key, void *dtls)
{
  assert(key);

  spinlock_lock(&key->lock);
  key->ref_count++;
  spinlock_unlock(&key->lock);

  struct dtls_value *v = NULL;
  TAILQ_FOREACH(v, &dtls_data->list, link)
    if(v->key == key) break;

  if(!v) {
    spinlock_lock(&__dtls_lock);
    v = kmem_cache_alloc(__dtls_values_cache, 0);
    spinlock_unlock(&__dtls_lock);
    assert(v);
    v->key = key;
    TAILQ_INSERT_HEAD(&dtls_data->list, v, link);
  }
  v->dtls = dtls;
}

static inline void *__get_dtls(dtls_data_t *dtls_data, dtls_key_t key)
{
  assert(key);

  struct dtls_value *v = NULL;
  TAILQ_FOREACH(v, &dtls_data->list, link)
    if(v->key == key) return v->dtls;
  return v;
}

static inline void __destroy_dtls(dtls_data_t *dtls_data)
{
 struct dtls_value *v,*n;
  v = TAILQ_FIRST(&dtls_data->list);
  while(v != NULL) {
    dtls_key_t key = v->key;
    bool run_dtor = false;
  
    spinlock_lock(&key->lock);
    if(key->valid)
      if(key->dtor)
        run_dtor = true;
    spinlock_unlock(&key->lock);

	// MUST run the dtor outside the spinlock if we want it to be able to call
	// code that may deschedule it for a while (i.e. a mutex). Probably a
	// good idea anyway since it can be arbitrarily long and is written by the
	// user. Note, there is a small race here on the valid field, whereby we
	// may run a destructor on an invalid key. At least the keys memory wont
	// be deleted though, as protected by the ref count. Any reasonable usage
	// of this interface should safeguard that a key is never destroyed before
	// all of the threads that use it have exited anyway.
    if(run_dtor) {
	  void *dtls = v->dtls;
      v->dtls = NULL;
      key->dtor(dtls);
    }

    spinlock_lock(&key->lock);
    key->ref_count--;
    spinlock_unlock(&key->lock);
    __maybe_free_dtls_key(key);

    n = TAILQ_NEXT(v, link);
    TAILQ_REMOVE(&dtls_data->list, v, link);
    spinlock_lock(&__dtls_lock);
    kmem_cache_free(__dtls_values_cache, v);
    spinlock_unlock(&__dtls_lock);
    v = n;
  }
}

void set_dtls(dtls_key_t key, void *dtls)
{
  bool initialized = true;
  dtls_data_t *dtls_data = NULL;
  if(!__dtls_initialized) {
    initialized = false;
    __dtls_initialized  = true;
  }
  dtls_data = &__dtls_data;
  if(!initialized) {
    TAILQ_INIT(&dtls_data->list);
  }
  __set_dtls(dtls_data, key, dtls);
}

void *get_dtls(dtls_key_t key)
{
  dtls_data_t *dtls_data = NULL;
  if(!__dtls_initialized)
    return NULL;
  dtls_data = &__dtls_data;
  return __get_dtls(dtls_data, key);
}

void destroy_dtls()
{
  dtls_data_t *dtls_data = NULL;
  if(!__dtls_initialized)
    return;
  dtls_data = &__dtls_data;
  __destroy_dtls(dtls_data);
}

