/* Copyright (c) 2012 The Regents of the University of California
 * Kevin Klues <klueska@cs.berkeley.edu>
 *
 * See LICENSE for details. */

#include <parlib/assert.h>
#include <parlib/dtls.h>
#include <parlib/slab.h>
#include <parlib/spinlock.h>
#include <stddef.h>

/* The current dymamic tls implementation uses a locked linked list
 * to find the key for a given thread. We should probably find a better way to
 * do this based on a custom lock-free hash table or something. */
#include <parlib/spinlock.h>
#include <sys/queue.h>

/* Define some number of static keys, for which the memory containing the keys
 * and the per-thread memory for the values associated with those keys is
 * allocated statically. This is adapted from glibc's notion of the
 * "specific_1stblock" field embedded directly into its pthread structure for
 * pthread_get/specific() calls. */
#define NUM_STATIC_KEYS 32

/* The dynamic tls key structure */
struct dtls_key {
	int id;
	int ref_count;
	bool valid;
	void (*dtor)(void *);
};

/* The definition of a dtls_key list and its elements */
struct dtls_value {
	TAILQ_ENTRY(dtls_value) link;
	struct dtls_key *key;
	const void *dtls;
};
TAILQ_HEAD(dtls_list, dtls_value);

/* A struct containing all of the per thread (i.e. vcore or uthread) data
 * associated with dtls */
typedef struct dtls_data {
	/* A per-thread list of dtls regions */
	struct dtls_list list;
	/* Memory to hold dtls values for the first NUM_STATIC_KEYS keys */
	struct dtls_value early_values[NUM_STATIC_KEYS];
} dtls_data_t;

/* A slab of dtls keys (global to all threads) */
static struct kmem_cache *__dtls_keys_cache;

/* A slab of values for use when mapping a dtls_key to its per-thread value */
struct kmem_cache *__dtls_values_cache;

static __thread dtls_data_t __dtls_data;
static __thread bool __dtls_initialized;
static struct dtls_key static_dtls_keys[NUM_STATIC_KEYS];
static int num_dtls_keys;

/* Initialize the slab caches for allocating dtls keys and values. */
int dtls_cache_init(void)
{
	/* Make sure this only runs once */
	static bool initialized;

	if (initialized)
		return 0;
	initialized = true;

	/* Initialize the global cache of dtls_keys */
	__dtls_keys_cache =
	    kmem_cache_create("dtls_keys_cache", sizeof(struct dtls_key),
			      __alignof__(struct dtls_key), 0, NULL, NULL,
			      NULL);

	/* Initialize the global cache of dtls_values */
	__dtls_values_cache =
	    kmem_cache_create("dtls_values_cache", sizeof(struct dtls_value),
			      __alignof__(struct dtls_value), 0, NULL, NULL,
			      NULL);

	return 0;
}

static dtls_key_t __allocate_dtls_key(void)
{
	dtls_key_t key;
	int keyid = __sync_fetch_and_add(&num_dtls_keys, 1);

	if (keyid < NUM_STATIC_KEYS) {
		key = &static_dtls_keys[keyid];
	} else {
		dtls_cache_init();
		key = kmem_cache_alloc(__dtls_keys_cache, 0);
	}
	assert(key);
	key->id = keyid;
	key->ref_count = 1;
	return key;
}

static void __maybe_free_dtls_key(dtls_key_t key)
{
	int ref_count = __sync_add_and_fetch(&key->ref_count, -1);

	if (ref_count == 0 && key->id >= NUM_STATIC_KEYS)
		kmem_cache_free(__dtls_keys_cache, key);
}

static struct dtls_value *__allocate_dtls_value(struct dtls_data *dtls_data,
                                                struct dtls_key *key)
{
	struct dtls_value *v;

	if (key->id < NUM_STATIC_KEYS)
		v = &dtls_data->early_values[key->id];
	else
		v = kmem_cache_alloc(__dtls_values_cache, 0);
	assert(v);
	return v;
}

static void __free_dtls_value(struct dtls_value *v)
{
	if (v->key->id >= NUM_STATIC_KEYS)
		kmem_cache_free(__dtls_values_cache, v);
}

dtls_key_t dtls_key_create(dtls_dtor_t dtor)
{
	dtls_key_t key = __allocate_dtls_key();

	key->valid = true;
	key->dtor = dtor;
	return key;
}

void dtls_key_delete(dtls_key_t key)
{
	assert(key);

	key->valid = false;
	__maybe_free_dtls_key(key);
}

static inline struct dtls_value *__get_dtls(dtls_data_t *dtls_data,
                                            dtls_key_t key)
{
	struct dtls_value *v;

	assert(key);
	if (key->id < NUM_STATIC_KEYS) {
		v = &dtls_data->early_values[key->id];
		if (v->key != NULL)
			return v;
	} else {
		TAILQ_FOREACH(v, &dtls_data->list, link)
			if (v->key == key)
				return v;
	}
	return NULL;
}

static inline void __set_dtls(dtls_data_t *dtls_data, dtls_key_t key,
                              const void *dtls)
{
	struct dtls_value *v;

	assert(key);
	v = __get_dtls(dtls_data, key);
	if (!v) {
		v = __allocate_dtls_value(dtls_data, key);
		__sync_fetch_and_add(&key->ref_count, 1);
		v->key = key;
		TAILQ_INSERT_HEAD(&dtls_data->list, v, link);
	}
	v->dtls = dtls;
}

static inline void __destroy_dtls(dtls_data_t *dtls_data)
{
	struct dtls_value *v, *n;
	dtls_key_t key;
	const void *dtls;

	v = TAILQ_FIRST(&dtls_data->list);
	while (v != NULL) {
		key = v->key;
		/* The dtor must be called outside of a spinlock so that it can
		 * call code that may deschedule it for a while (i.e. a mutex).
		 * Probably a good idea anyway since it can be arbitrarily long
		 * and is written by the user. Note, there is a small race here
		 * on the valid field, whereby we may run a destructor on an
		 * invalid key. At least the keys memory wont be deleted though,
		 * as protected by the ref count. Any reasonable usage of this
		 * interface should safeguard that a key is never destroyed
		 * before all of the threads that use it have exited anyway. */
		if (key->valid && key->dtor) {
			dtls = v->dtls;
			v->dtls = NULL;
			key->dtor((void*)dtls);
		}
		n = TAILQ_NEXT(v, link);
		TAILQ_REMOVE(&dtls_data->list, v, link);
		/* Free both the key (which is v->key) and v *after* removing v
		 * from the list.  It's possible that free() will call back into
		 * the DTLS (e.g.  pthread_getspecific()), and v must be off the
		 * list by then.
		 *
		 * For a similar, hilarious bug in glibc, check out:
		 * https://sourceware.org/bugzilla/show_bug.cgi?id=3317 */
		__maybe_free_dtls_key(key);
		__free_dtls_value(v);
		v = n;
	}
}

void set_dtls(dtls_key_t key, const void *dtls)
{
	bool initialized = true;
	dtls_data_t *dtls_data = NULL;

	if (!__dtls_initialized) {
		initialized = false;
		__dtls_initialized = true;
	}
	dtls_data = &__dtls_data;
	if (!initialized)
		TAILQ_INIT(&dtls_data->list);
	__set_dtls(dtls_data, key, dtls);
}

void *get_dtls(dtls_key_t key)
{
	dtls_data_t *dtls_data = NULL;
	struct dtls_value *v;

	if (!__dtls_initialized)
		return NULL;
	dtls_data = &__dtls_data;
	v = __get_dtls(dtls_data, key);
	return v ? (void*)v->dtls : NULL;
}

void destroy_dtls(void)
{
	dtls_data_t *dtls_data = NULL;

	if (!__dtls_initialized)
		return;
	dtls_data = &__dtls_data;
	__destroy_dtls(dtls_data);
}
