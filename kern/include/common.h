#pragma once

#include <ros/common.h>
#include <compiler.h>

#ifndef __ASSEMBLER__
#include <sys/types.h>
#include <linux/overflow.h>
#endif

/* Force a rebuild of the whole kernel if 64BIT-ness changed */
#ifdef CONFIG_64BIT
#endif

#define PASTE_THEM(x, y) x ## y
#define PASTE(x, y) PASTE_THEM(x, y)

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
/* Aligns x down to the mask, e.g. (2^6 - 1)
 * (round down if any mask bits are set)*/
#define __ALIGN_MASK_DOWN(x, mask) ((uintptr_t)(x) & ~(mask))
/* Aligns x down to the alignment, e.g. 2^6. */
#define ALIGN_DOWN(x, a) ((typeof(x)) __ALIGN_MASK_DOWN(x, (a) - 1))
/* Will return false for 0.  Debatable, based on what you want. */
#define IS_PWR2(x) ((x) && !((x) & (x - 1)))

#define ARRAY_SIZE(x) COUNT_OF(x)

/* Makes sure func is run exactly once.  Can handle concurrent callers, and
 * other callers spin til the func is complete. */
#define run_once(func)                                                         \
do {                                                                           \
	static bool ran_once = FALSE;                                          \
	static bool is_running = FALSE;                                        \
	if (!ran_once) {                                                       \
		/* fetch and set TRUE, w/o a header or test_and_set weirdness*/\
		if (!__sync_fetch_and_or(&is_running, TRUE)) {                 \
			/* we won the race and get to run the func */          \
			func;                                                  \
			/* don't let the ran_once write pass previous writes */\
			wmb();                                                 \
			ran_once = TRUE;                                       \
		} else {                                                       \
			/* someone else won */                                 \
			while (!ran_once)                                      \
				cpu_relax();                                   \
		}                                                              \
	}                                                                      \
} while (0)

/* Unprotected, single-threaded version, makes sure func is run exactly once */
#define run_once_racy(func)                                                    \
do {                                                                           \
	static bool ran_once = FALSE;                                          \
	if (!ran_once) {                                                       \
		func;                                                          \
		ran_once = TRUE;                                               \
	}                                                                      \
} while (0)

#ifndef __ASSEMBLER__

/* Returns the least common multiple of x and p2; p2 is a power of 2.  Returns 0
 * on error, including non-power-of-2, overflow, or a 0 input. */
static inline unsigned long LCM_PWR2(unsigned long x, unsigned long p2)
{
	/* All multiples of p2, which has exactly one bit set, will have zeros
	 * for the bits below its set bit.  The LCM will be x, shifted left as
	 * little as possible, such that it has no bits below p2's set bit.
	 * Each shift is a multiplication by 2, which is the only prime factor
	 * if p2. */
	int p2_bit, x_first_bit;
	unsigned long ret;

	if (!x || !IS_PWR2(p2))
		return 0;
	p2_bit = __builtin_ffsl(p2);
	x_first_bit = __builtin_ffsl(x);
	if (x_first_bit >= p2_bit)
		return x;
	if (check_shl_overflow(x, p2_bit - x_first_bit, &ret))
		return 0;
	return ret;
}

static inline uint32_t low32(uint64_t val)
{
	return val & 0xffffffff;
}

static inline uint32_t high32(uint64_t val)
{
	return val >> 32;
}

#endif /* !__ASSEMBLER__ */
