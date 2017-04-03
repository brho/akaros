#pragma once

#include <ros/common.h>
#include <compiler.h>

/* Force a rebuild of the whole kernel if 64BIT-ness changed */
#ifdef CONFIG_64BIT
#endif

#define SIZE_MAX        (~(size_t)0)

// Efficient min and max operations
#define MIN(_a, _b)						\
({								\
	typeof(_a) __a = (_a);					\
	typeof(_b) __b = (_b);					\
	__a <= __b ? __a : __b;					\
})
#define MAX(_a, _b)						\
({								\
	typeof(_a) __a = (_a);					\
	typeof(_b) __b = (_b);					\
	__a >= __b ? __a : __b;					\
})

/* Test for alignment, e.g. 2^6 */
#define ALIGNED(p, a)	(!(((uintptr_t)(p)) & ((a)-1)))
/* Aligns x up to the mask, e.g. (2^6 - 1) (round up if any mask bits are set)*/
#define __ALIGN_MASK(x, mask) (((uintptr_t)(x) + (mask)) & ~(mask))
/* Aligns x up to the alignment, e.g. 2^6. */
#define ALIGN(x, a) ((typeof(x)) __ALIGN_MASK(x, (a) - 1))
/* Will return false for 0.  Debatable, based on what you want. */
#define IS_PWR2(x) ((x) && !((x) & (x - 1)))

#define ARRAY_SIZE(x) COUNT_OF(x)
