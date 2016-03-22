/* Copyright (c) 2016 Google Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details. */

#pragma once

#include <parlib/uthread.h>

__BEGIN_DECLS

struct guest_thread {
	struct uthread				uthread;
};

__END_DECLS
