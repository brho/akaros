/* Copyright (c) 2015 Google Inc
 * Davide Libenzi <dlibenzi@google.com>
 * See LICENSE for details.
 */

#include <sys/types.h>
#include <arch/topology.h>
#include <kmalloc.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <percpu.h>

char *percpu_base;

static void run_init_functions(void)
{
	extern char __attribute__((weak)) PERCPU_INIT_START_VAR[];
	extern char __attribute__((weak)) PERCPU_INIT_STOP_VAR[];

	if (PERCPU_INIT_START_VAR) {
		void (**pfunc)(void) = (void (**)(void)) PERCPU_INIT_START_VAR;
		void (**pfunc_top)(void) = (void (**)(void)) PERCPU_INIT_STOP_VAR;

		for (; pfunc < pfunc_top; pfunc++)
			(*pfunc)();
	}
}

void percpu_init(void)
{
	assert(num_cores > 0);
	percpu_base = kmalloc(num_cores * PERCPU_SIZE, 0);
	assert(percpu_base);

	if (PERCPU_START_VAR) {
		for (int i = 0; i < num_cores; i++)
			memcpy(percpu_base + i * PERCPU_SIZE, PERCPU_START_VAR,
				   PERCPU_SIZE);
	}
	run_init_functions();
}
