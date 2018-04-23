/* Copyright (c) 2015 Google Inc
 * Davide Libenzi <dlibenzi@google.com>
 * See LICENSE for details.
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
 */

#pragma once

#include <sys/types.h>
#include <arch/topology.h>
#include <ros/common.h>

#define PERCPU_SECTION __percpu
#define PERCPU_SECTION_STR STRINGIFY(PERCPU_SECTION)

#define PERCPU_VARNAME(var) PASTE(__percpu_, var)

#define PERCPU_START_VAR PASTE(__start_, PERCPU_SECTION)
#define PERCPU_STOP_VAR PASTE(__stop_, PERCPU_SECTION)

#define PERCPU_SIZE (PERCPU_STOP_VAR - PERCPU_START_VAR)
#define PERCPU_OFFSET(var) ((char *) &(var) - PERCPU_START_VAR)

#define __PERCPU_VARPTR(var, cpu)										\
	({																	\
		typeof(var) *__cv;												\
		if (likely(percpu_base))										\
			__cv = (typeof(var) *) (percpu_base + cpu * PERCPU_SIZE +	\
			                            PERCPU_OFFSET(var));				\
		else															\
			__cv = &var;												\
		__cv;															\
	})
#define _PERCPU_VARPTR(var, cpu) __PERCPU_VARPTR(PERCPU_VARNAME(var), cpu)
#define PERCPU_VARPTR(var) __PERCPU_VARPTR(PERCPU_VARNAME(var), core_id())

#define _PERCPU_VAR(var, cpu) (*__PERCPU_VARPTR(PERCPU_VARNAME(var), cpu))
#define PERCPU_VAR(var) (*__PERCPU_VARPTR(PERCPU_VARNAME(var), core_id()))

#define DEFINE_PERCPU(type, var)						\
	type PERCPU_VARNAME(var) __attribute__ ((section (PERCPU_SECTION_STR)))
#define DECLARE_PERCPU(type, var)								\
	extern type PERCPU_VARNAME(var)								\
		__attribute__ ((section (PERCPU_SECTION_STR)))

#define PERCPU_INIT_SECTION __percpu_init
#define PERCPU_INIT_SECTION_STR STRINGIFY(PERCPU_INIT_SECTION)

#define PERCPU_INIT_START_VAR PASTE(__start_, PERCPU_INIT_SECTION)
#define PERCPU_INIT_STOP_VAR PASTE(__stop_, PERCPU_INIT_SECTION)

#define DEFINE_PERCPU_INIT(func)										\
	static void func(void);												\
	void (* const PERCPU_VARNAME(func))(void)							\
		__attribute__ ((section (PERCPU_INIT_SECTION_STR))) = (func)

extern char __attribute__((weak)) PERCPU_START_VAR[];
extern char __attribute__((weak)) PERCPU_STOP_VAR[];
extern char *percpu_base;

void percpu_init(void);
