/* Copyright (c) 2016 Google Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Set and query the existence of cpu features.
 *
 * Note that I didn't provide a "cpu_clr_feat()" yet.  These are intended to be
 * write-once, read-many. */

#pragma once

#include <ros/procinfo.h>
#include <bitops.h>

static inline bool cpu_has_feat(int feature)
{
	return test_bit(feature, __proc_global_info.cpu_feats);
}

static inline void cpu_set_feat(int feature)
{
	__set_bit(feature, __proc_global_info.cpu_feats);
}
