/* Copyright (c) 2015 Google Inc
 * Davide Libenzi <dlibenzi@google.com>
 * See LICENSE for details.
 */

#pragma once

#include <sys/types.h>
#include <stdio.h>

#define ADDRESS_RANGE(s, e) { .start = (s), .end = (e) }

struct address_range {
	uintptr_t start;
	uintptr_t end;
};

int address_range_validate(const struct address_range *ars, size_t count);
int address_range_init(struct address_range *ars, size_t count);
const struct address_range *address_range_find(const struct address_range *ars,
					       size_t count, uintptr_t addr);
