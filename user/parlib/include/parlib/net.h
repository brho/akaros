/* Copyright (c) 2014 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Networking helpers for dealing with the plan 9 interface. */

#pragma once

#include <stdbool.h>

__BEGIN_DECLS

static inline bool snprintf_error(int ret, size_t buf_len)
{
	return ret < 0 || ret >= buf_len;
}

int cheap_dial(char *addr, char *local, char *dir, int *cfdp);

__END_DECLS
