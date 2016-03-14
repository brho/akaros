/* Copyright (c) 2016 Google Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Query the existence of cpu features. */

#pragma once

#include <ros/procinfo.h>

static inline bool cpu_has_feat(int feature)
{
	size_t bits_per_long = sizeof(unsigned long) * 8;
	unsigned long *cpu_feats = __proc_global_info.cpu_feats;

	return (cpu_feats[feature / bits_per_long] &
	        (1UL << (feature % bits_per_long))) != 0;
}
