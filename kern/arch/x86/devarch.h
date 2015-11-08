/* Copyright (c) 2015 Google Inc
 * Davide Libenzi <dlibenzi@google.com>
 * See LICENSE for details.
 */

#pragma once

#include <sys/types.h>
#include <stdio.h>
#include <core_set.h>

uint64_t *coreset_read_msr(const struct core_set *cset, uint32_t addr,
						   size_t *nvalues);
int coreset_write_msr(const struct core_set *cset, uint32_t addr,
					  uint64_t value);
