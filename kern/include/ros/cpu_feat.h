/* Copyright (c) 2016 Google Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * CPU feature detection.
 *
 * You can add new items as needed.  Changing __CPU_FEAT_ARCH_START will require
 * a rebuild of the world.  Otherwise, you just need to reinstall kernel
 * headers. */

#pragma once

#include <ros/common.h>

#define CPU_FEAT_VMM		1
#define __CPU_FEAT_ARCH_START	64

#include <ros/arch/cpu_feat.h>

#define __NR_CPU_FEAT_BITS DIV_ROUND_UP(__NR_CPU_FEAT,                         \
                                        sizeof(unsigned long) * 8)
