/* Copyright (c) 2015 Google Inc
 * Davide Libenzi <dlibenzi@google.com>
 * See LICENSE for details.
 */

#pragma once

#include <ros/arch/arch.h>
#include <ros/common.h>
#include <sys/types.h>
#include <stdint.h>
#include <stdio.h>
#include <limits.h>
#include <parlib/parlib.h>
#include <parlib/bitmask.h>

#define CORE_SET_SIZE BYTES_FOR_BITMASK(MAX_NUM_CORES)

/* Not using sched.h CPU set because that file has definitions for a large
 * number of APIs Akaros does not support.
 * Making Akaros core_set.h visible in userslace might be a cleaner approach.
 */
struct core_set {
	DECL_BITMASK(core_set, MAX_NUM_CORES);
};

void ros_get_low_latency_core_set(struct core_set *cores);
size_t ros_get_low_latency_core_count(void);
size_t ros_total_cores(void);
void ros_parse_cores(const char *str, struct core_set *cores);
void ros_get_all_cores_set(struct core_set *cores);
void ros_not_core_set(struct core_set *dcs);
void ros_and_core_sets(struct core_set *dcs, const struct core_set *scs);
void ros_or_core_sets(struct core_set *dcs, const struct core_set *scs);

static inline void ros_set_bit(void *addr, size_t nbit)
{
	SET_BITMASK_BIT(addr, nbit);
}

static inline void ros_clear_bit(void *addr, size_t nbit)
{
	CLR_BITMASK_BIT(addr, nbit);
}

static inline bool ros_get_bit(const void *addr, size_t nbit)
{
	return GET_BITMASK_BIT(addr, nbit);
}
