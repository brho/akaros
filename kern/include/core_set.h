/* Copyright (c) 2015 Google Inc
 * Davide Libenzi <dlibenzi@google.com>
 * See LICENSE for details.
 */

#pragma once

#include <arch/arch.h>
#include <arch/topology.h>
#include <ros/common.h>
#include <stdio.h>
#include <string.h>
#include <bitops.h>

struct core_set {
	unsigned long cpus[DIV_ROUND_UP(MAX_NUM_CORES, BITS_PER_LONG)];
};

static inline void core_set_init(struct core_set *cset)
{
	memset(cset, 0, sizeof(*cset));
}

static inline void core_set_setcpu(struct core_set *cset, unsigned int cpuno)
{
	__set_bit(cpuno, cset->cpus);
}

static inline void core_set_clearcpu(struct core_set *cset, unsigned int cpuno)
{
	__clear_bit(cpuno, cset->cpus);
}

static inline bool core_set_getcpu(const struct core_set *cset,
				   unsigned int cpuno)
{
	return test_bit(cpuno, cset->cpus);
}

static inline void core_set_fill_available(struct core_set *cset)
{
	for (int i = 0; i < num_cores; i++)
		core_set_setcpu(cset, i);
}

static inline int core_set_count(const struct core_set *cset)
{
	int count = 0;

	for (size_t i = 0; i < ARRAY_SIZE(cset->cpus); i++) {
		for (unsigned long v = cset->cpus[i]; v; v >>= 1)
			count += (int) (v & 1);
	}

	return count;
}

static inline int core_set_remote_count(const struct core_set *cset)
{
	int count = core_set_count(cset), cpu = core_id();

	return core_set_getcpu(cset, cpu) ? count - 1: count;
}
