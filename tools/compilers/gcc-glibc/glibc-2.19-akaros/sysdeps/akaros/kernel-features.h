/* Copyright (c) 2016 Google Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Glibc uses this header to set a bunch of #defines to show what is supported
 * on a particular kernel version.  They set things like __ASSUME_AT_RANDOM.
 *
 * On Akaros, we'll eventually set those too.  This is also a good place for
 * exporting all of the cpu feature detection to glibc.
 *
 * Note that this file is only included within glibc itself; it won't appear in
 * the sysroot.  Use parlib/cpu_feat.h for that. */

#pragma once

#include <ros/procinfo.h>

static inline bool cpu_has_feat(int feature)
{
	size_t bits_per_long = sizeof(unsigned long) * 8;
	unsigned long *cpu_feats = __proc_global_info.cpu_feats;

	return (cpu_feats[feature / bits_per_long] &
	        (1UL << (feature % bits_per_long))) != 0;
}
