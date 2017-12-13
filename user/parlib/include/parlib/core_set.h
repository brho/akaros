/* Copyright (c) 2015 Google Inc
 * Davide Libenzi <dlibenzi@google.com>
 * See LICENSE for details.
 */

#pragma once

#include <parlib/bitmask.h>

__BEGIN_DECLS

#define CORE_SET_SIZE BYTES_FOR_BITMASK(MAX_NUM_CORES)

struct core_set {
	DECL_BITMASK(core_set, MAX_NUM_CORES);
};

void parlib_get_ll_core_set(struct core_set *cores);
size_t parlib_nr_ll_cores(void);
size_t parlib_nr_total_cores(void);
void parlib_parse_cores(const char *str, struct core_set *cores);
void parlib_get_all_core_set(struct core_set *cores);
void parlib_get_none_core_set(struct core_set *cores);
void parlib_not_core_set(struct core_set *dcs);
void parlib_and_core_sets(struct core_set *dcs, const struct core_set *scs);
void parlib_or_core_sets(struct core_set *dcs, const struct core_set *scs);

static inline void parlib_set_core(struct core_set *cores, size_t coreid)
{
	SET_BITMASK_BIT(cores->core_set, coreid);
}

static inline void parlib_clear_core(struct core_set *cores, size_t coreid)
{
	CLR_BITMASK_BIT(cores->core_set, coreid);
}

static inline bool parlib_get_core(const struct core_set *cores, size_t coreid)
{
	return GET_BITMASK_BIT(cores->core_set, coreid);
}

__END_DECLS
