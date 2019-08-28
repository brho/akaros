/* Copyright (c) 2015 Google Inc
 * Davide Libenzi <dlibenzi@google.com>
 * See LICENSE for details.
 *
 * Static percpu variables:
 *
 * The per CPU utility macros allow file local declaration of per CPU variables.
 * When a struct my_struct needs to have a per CPU instance, one would declare
 * something like:
 *
 *   static DEFINE_PERCPU(struct my_struct, my_data);
 *
 * The per CPU data can then be accessed with either of those forms:
 *
 *   struct my_struct *ptr = PERCPU_VARPTR(my_data);
 *   PERCPU_VAR(my_data).field = 17;
 *
 * When the per CPU data has complex initialization, it is possible to register
 * functions which will be called immediately after the per CPU data is created:
 *
 *   DEFINE_PERCPU_INIT(my_init);
 *
 * Then the my_init() function would just:
 *
 *   static void my_init(void)
 *   {
 *       for (int i = 0; i < num_cores; i++) {
 *           struct my_struct *ptr = _PERCPU_VARPTR(my_data, i);
 *
 *           // Initialize ptr data
 *       }
 *   }
 *
 *
 * Dynamic percpu variables:
 *
 * You can also declare per-cpu variables dynamically, though it's not quite the
 * same as the static variables.  Careful - We return *pointers*, and our users
 * need to dereference them when using any of the PERCPU_ helpers.
 *
 * Example (per core u64s)  Note each *use* dereferences 'foos':
 *
 * uint64_t *foos = percpu_zalloc(uint64_t, MEM_WAIT);
 *
 * // Each core increments
 * PERCPU_VAR(*foos)++;
 *
 * // One core can print them all out
 * for_each_core(i)
 *	printk("Addr %p, value %lu\n", _PERCPU_VARPTR(*foos, i),
 *	       _PERCPU_VAR(*foos, i));
 *
 * // Free, but don't deref here.  'foos' is your handle.
 * percpu_free(foos);
 */

#pragma once

#include <sys/types.h>
#include <arch/topology.h>
#include <ros/common.h>

#define PERCPU_SECTION __percpu_section
#define PERCPU_SECTION_STR STRINGIFY(PERCPU_SECTION)

#define PERCPU_START_VAR PASTE(__start_, PERCPU_SECTION)
#define PERCPU_STOP_VAR PASTE(__stop_, PERCPU_SECTION)

#define PERCPU_DYN_SIZE 1024
#define PERCPU_STATIC_SIZE (PERCPU_STOP_VAR - PERCPU_START_VAR)
#define PERCPU_SIZE (PERCPU_STATIC_SIZE + PERCPU_DYN_SIZE)
#define PERCPU_OFFSET(var) ((char *) &(var) - PERCPU_START_VAR)

#define __PERCPU_VARPTR(var, cpu)					\
({									\
	typeof(var) *__cv;						\
	if (likely(percpu_base))					\
		__cv = (typeof(var) *) (percpu_base + cpu * PERCPU_SIZE + \
					PERCPU_OFFSET(var));		\
	else								\
		__cv = &var;						\
	__cv;								\
})
#define _PERCPU_VARPTR(var, cpu) __PERCPU_VARPTR(var, cpu)
#define PERCPU_VARPTR(var) __PERCPU_VARPTR(var, core_id())

#define _PERCPU_VAR(var, cpu) (*__PERCPU_VARPTR(var, cpu))
#define PERCPU_VAR(var) (*__PERCPU_VARPTR(var, core_id()))

#define DEFINE_PERCPU(type, var)						\
	__typeof__(type) var __attribute__ ((section (PERCPU_SECTION_STR)))
#define DECLARE_PERCPU(type, var)					\
	extern __typeof__(type) var					\
		__attribute__ ((section (PERCPU_SECTION_STR)))

#define PERCPU_INIT_SECTION __percpu_init
#define PERCPU_INIT_SECTION_STR STRINGIFY(PERCPU_INIT_SECTION)

#define PERCPU_INIT_START_VAR PASTE(__start_, PERCPU_INIT_SECTION)
#define PERCPU_INIT_STOP_VAR PASTE(__stop_, PERCPU_INIT_SECTION)

#define PERCPU_INIT_NAME(func) PASTE(__percpu_, func)
#define DEFINE_PERCPU_INIT(func)					\
	static void func(void);						\
	void (* const PERCPU_INIT_NAME(func))(void)			\
		__attribute__ ((section (PERCPU_INIT_SECTION_STR))) = (func)

extern char __attribute__((weak)) PERCPU_START_VAR[];
extern char __attribute__((weak)) PERCPU_STOP_VAR[];
extern char *percpu_base;

void percpu_init(void);

#define percpu_alloc(x, flags) __percpu_alloc(sizeof(x), __alignof__(x), flags)
#define percpu_zalloc(x, flags) __percpu_zalloc(sizeof(x), __alignof__(x), \
                                                flags)
#define percpu_free(x) __percpu_free(x, sizeof(*x))

void *__percpu_alloc(size_t size, size_t align, int flags);
void *__percpu_zalloc(size_t size, size_t align, int flags);
void __percpu_free(void *base, size_t size);
