/* Copyright (c) 2015 Google Inc
 * Davide Libenzi <dlibenzi@google.com>
 * See LICENSE for details.
 */

#pragma once

#include <kthread.h>

struct completion {
	struct cond_var cv;
	int count;
};

void completion_init(struct completion *comp, int count);
void completion_complete(struct completion *comp, int how_much);
void completion_wait(struct completion *comp);
