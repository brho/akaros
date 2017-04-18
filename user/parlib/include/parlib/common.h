/* Copyright (c) 2015 Google Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Common helpers for Akaros user programs. */

#pragma once

#include <ros/common.h>

#define IS_PWR2(x) ((x) && !((x) & (x - 1)))
