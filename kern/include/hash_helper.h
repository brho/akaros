/* Copyright (c) 2016 Google Inc
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Helper structs and funcs for making a dynamically-sized hash table. */

#pragma once

struct hash_helper {
	unsigned int			nr_hash_lists;
	unsigned int			nr_hash_bits;
	size_t				nr_items;
	size_t				load_limit;
};

#define HASH_MAX_LOAD_FACTOR(size) ((size * 13) / 20)
#define HASH_INIT_NR_BITS 6
#define HASH_MAX_NR_BITS 31
#define HASH_INIT_SZ (1 << HASH_INIT_NR_BITS)

static inline bool hash_needs_more(struct hash_helper *hh)
{
	if (hh->nr_hash_bits == HASH_MAX_NR_BITS)
		return FALSE;
	return hh->nr_items > hh->load_limit;
}

/* Only call when you know we need more (i.e., bits < 31). */
static inline unsigned int hash_next_nr_lists(struct hash_helper *hh)
{
	return 1 << (hh->nr_hash_bits + 1);
}

/* Only call when you know we need more (i.e., bits < 31). */
static inline void hash_incr_nr_lists(struct hash_helper *hh)
{
	hh->nr_hash_bits++;
	hh->nr_hash_lists = 1 << hh->nr_hash_bits;
}

static inline void hash_set_load_limit(struct hash_helper *hh, size_t lim)
{
	hh->load_limit = lim;
}

static inline void hash_reset_load_limit(struct hash_helper *hh)
{
	hh->load_limit = HASH_MAX_LOAD_FACTOR(hh->nr_hash_lists);
}

static inline void hash_init_hh(struct hash_helper *hh)
{
	hh->nr_hash_lists = HASH_INIT_SZ;
	hh->nr_hash_bits = HASH_INIT_NR_BITS;
	hh->nr_items = 0;
	hash_reset_load_limit(hh);
}
