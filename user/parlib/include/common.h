/* Copyright (c) 2015 Google Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Common helpers for Akaros user programs. */

#ifndef PARLIB_COMMON_H
#define PARLIB_COMMON_H

#include <ros/common.h>
#include <parlib/assert.h>

#define IS_PWR2(x) ((x) && !((x) & (x - 1)))

#endif /* PARLIB_COMMON_H */
