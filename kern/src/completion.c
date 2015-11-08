/* Copyright (c) 2015 Google Inc
 * Davide Libenzi <dlibenzi@google.com>
 * See LICENSE for details.
 */

#include <kthread.h>
#include <completion.h>

void completion_init(struct completion *comp, int count)
{
	cv_init_irqsave(&comp->cv);
	comp->count = count;
}

void completion_complete(struct completion *comp, int how_much)
{
	int8_t state = 0;

	cv_lock_irqsave(&comp->cv, &state);
	comp->count -= how_much;
	if (comp->count <= 0)
		__cv_broadcast(&comp->cv);
	cv_unlock_irqsave(&comp->cv, &state);
}

void completion_wait(struct completion *comp)
{
	int8_t state = 0;

	cv_lock_irqsave(&comp->cv, &state);
	if (comp->count > 0) {
		cv_wait_and_unlock(&comp->cv);
		enable_irqsave(&state);
	} else {
		cv_unlock_irqsave(&comp->cv, &state);
	}
}
