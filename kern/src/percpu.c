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
#include <arena.h>
#include <page_alloc.h>
#include <smp.h>

char *percpu_base;
static struct arena *pcpu_dyn_arena;

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

	percpu_base = kpages_alloc(num_cores * PERCPU_SIZE, MEM_WAIT);
	if (PERCPU_START_VAR) {
		for (int i = 0; i < num_cores; i++)
			memcpy(percpu_base + i * PERCPU_SIZE, PERCPU_START_VAR,
				   PERCPU_STATIC_SIZE);
	}
	/* We hand out addresses starting right above the static section, which ends
	 * at PERCPU_STOP_VAR. */
	pcpu_dyn_arena = arena_create("pcpu_dyn", PERCPU_STOP_VAR, PERCPU_DYN_SIZE,
	                              1, NULL, NULL, NULL, 0, MEM_WAIT);
	assert(pcpu_dyn_arena);
	run_init_functions();
}

/* We return pointers, but our users need to dereference them when using any of
 * the PERCPU_ helpers so that they are treated like the static vars. */
void *__percpu_alloc(size_t size, size_t align, int flags)
{
	assert(pcpu_dyn_arena);
	/* our alignment is limited to the alignment of percpu_base */
	warn_on(align > PGSIZE);
	return arena_xalloc(pcpu_dyn_arena, size, align, 0, 0, NULL, NULL,
	                    flags | ARENA_BESTFIT);
}

void *__percpu_zalloc(size_t size, size_t align, int flags)
{
	/* Yikes! */
	struct {
		uint8_t data[size];
	} *ret;

	ret = __percpu_alloc(size, align, flags);
	if (!ret)
		return NULL;
	for_each_core(i)
		memset(_PERCPU_VARPTR(*ret, i), 0, size);
	return ret;
}

void __percpu_free(void *base, size_t size)
{
	arena_free(pcpu_dyn_arena, base, size);
}
