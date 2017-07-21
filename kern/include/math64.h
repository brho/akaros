/* Copyright (C) 1991-2016, the Linux Kernel authors
 *
 * This source code is licensed under the GNU General Public License
 * Version 2. See the file COPYING for more details.
 */

#pragma once

#include <sys/types.h>
#include <arch/div64.h>

#if BITS_PER_LONG == 64

#define div64_long(x, y) div64_s64((x), (y))
#define div64_ul(x, y)   div64_u64((x), (y))

/**
 * div_u64_rem - unsigned 64bit divide with 32bit divisor with remainder
 *
 * This is commonly provided by 32bit archs to provide an optimized 64bit
 * divide.
 */
static inline uint64_t div_u64_rem(uint64_t dividend, uint32_t divisor,
                                   uint32_t *remainder)
{
	*remainder = dividend % divisor;
	return dividend / divisor;
}

/**
 * div_s64_rem - signed 64bit divide with 32bit divisor with remainder
 */
static inline int64_t div_s64_rem(int64_t dividend, int32_t divisor,
                                  int32_t *remainder)
{
	*remainder = dividend % divisor;
	return dividend / divisor;
}

/**
 * div64_u64_rem - unsigned 64bit divide with 64bit divisor and remainder
 */
static inline uint64_t div64_u64_rem(uint64_t dividend, uint64_t divisor,
                                     uint64_t *remainder)
{
	*remainder = dividend % divisor;
	return dividend / divisor;
}

/**
 * div64_u64 - unsigned 64bit divide with 64bit divisor
 */
static inline uint64_t div64_u64(uint64_t dividend, uint64_t divisor)
{
	return dividend / divisor;
}

/**
 * div64_s64 - signed 64bit divide with 64bit divisor
 */
static inline int64_t div64_s64(int64_t dividend, int64_t divisor)
{
	return dividend / divisor;
}

#elif BITS_PER_LONG == 32

#define div64_long(x, y) div_s64((x), (y))
#define div64_ul(x, y)   div_u64((x), (y))

#ifndef div_u64_rem
static inline uint64_t div_u64_rem(uint64_t dividend, uint32_t divisor,
                                   uint32_t *remainder)
{
	*remainder = do_div(dividend, divisor);
	return dividend;
}
#endif

#ifndef div_s64_rem
extern int64_t div_s64_rem(int64_t dividend, int32_t divisor,
                           int32_t *remainder);
#endif

#ifndef div64_u64_rem
extern uint64_t div64_u64_rem(uint64_t dividend, uint64_t divisor,
                              uint64_t *remainder);
#endif

#ifndef div64_u64
extern uint64_t div64_u64(uint64_t dividend, uint64_t divisor);
#endif

#ifndef div64_s64
extern int64_t div64_s64(int64_t dividend, int64_t divisor);
#endif

#endif /* BITS_PER_LONG */

/**
 * div_u64 - unsigned 64bit divide with 32bit divisor
 *
 * This is the most common 64bit divide and should be used if possible,
 * as many 32bit archs can optimize this variant better than a full 64bit
 * divide.
 */
#ifndef div_u64
static inline uint64_t div_u64(uint64_t dividend, uint32_t divisor)
{
	uint32_t remainder;
	return div_u64_rem(dividend, divisor, &remainder);
}
#endif

/**
 * div_s64 - signed 64bit divide with 32bit divisor
 */
#ifndef div_s64
static inline int64_t div_s64(int64_t dividend, int32_t divisor)
{
	int32_t remainder;
	return div_s64_rem(dividend, divisor, &remainder);
}
#endif

uint32_t iter_div_u64_rem(uint64_t dividend, uint32_t divisor,
                          uint64_t *remainder);

static __always_inline uint32_t
__iter_div_u64_rem(uint64_t dividend, uint32_t divisor, uint64_t *remainder)
{
	uint32_t ret = 0;

	while (dividend >= divisor) {
		/* The following asm() prevents the compiler from
		   optimising this loop into a modulo operation.  */
		asm("" : "+rm"(dividend));

		dividend -= divisor;
		ret++;
	}

	*remainder = dividend;

	return ret;
}

#ifndef mul_u32_u32
/*
 * Many a GCC version messes this up and generates a 64x64 mult :-(
 */
static inline uint64_t mul_u32_u32(uint32_t a, uint32_t b)
{
	return (uint64_t)a * b;
}
#endif

#if defined(CONFIG_ARCH_SUPPORTS_INT128) && defined(__SIZEOF_INT128__)

#ifndef mul_u64_u32_shr
static inline uint64_t mul_u64_u32_shr(uint64_t a, uint32_t mul,
                                       unsigned int shift)
{
	return (uint64_t)(((unsigned __int128)a * mul) >> shift);
}
#endif /* mul_u64_u32_shr */

#ifndef mul_u64_u64_shr
static inline uint64_t mul_u64_u64_shr(uint64_t a, uint64_t mul,
                                       unsigned int shift)
{
	return (uint64_t)(((unsigned __int128)a * mul) >> shift);
}
#endif /* mul_u64_u64_shr */

#else

#ifndef mul_u64_u32_shr
static inline uint64_t mul_u64_u32_shr(uint64_t a, uint32_t mul,
                                       unsigned int shift)
{
	uint32_t ah, al;
	uint64_t ret;

	al = a;
	ah = a >> 32;

	ret = mul_u32_u32(al, mul) >> shift;
	if (ah)
		ret += mul_u32_u32(ah, mul) << (32 - shift);

	return ret;
}
#endif /* mul_u64_u32_shr */

#ifndef mul_u64_u64_shr
static inline uint64_t mul_u64_u64_shr(uint64_t a, uint64_t b,
                                       unsigned int shift)
{
	union {
		uint64_t ll;
		struct {
#ifdef __BIG_ENDIAN
			uint32_t high, low;
#else
			uint32_t low, high;
#endif
		} l;
	} rl, rm, rn, rh, a0, b0;
	uint64_t c;

	a0.ll = a;
	b0.ll = b;

	rl.ll = mul_u32_u32(a0.l.low, b0.l.low);
	rm.ll = mul_u32_u32(a0.l.low, b0.l.high);
	rn.ll = mul_u32_u32(a0.l.high, b0.l.low);
	rh.ll = mul_u32_u32(a0.l.high, b0.l.high);

	/*
	 * Each of these lines computes a 64-bit intermediate result into "c",
	 * starting at bits 32-95.  The low 32-bits go into the result of the
	 * multiplication, the high 32-bits are carried into the next step.
	 */
	rl.l.high = c = (uint64_t)rl.l.high + rm.l.low + rn.l.low;
	rh.l.low = c = (c >> 32) + rm.l.high + rn.l.high + rh.l.low;
	rh.l.high = (c >> 32) + rh.l.high;

	/*
	 * The 128-bit result of the multiplication is in rl.ll and rh.ll,
	 * shift it right and throw away the high part of the result.
	 */
	if (shift == 0)
		return rl.ll;
	if (shift < 64)
		return (rl.ll >> shift) | (rh.ll << (64 - shift));
	return rh.ll >> (shift & 63);
}
#endif /* mul_u64_u64_shr */

#endif

#ifndef mul_u64_u32_div
static inline uint64_t mul_u64_u32_div(uint64_t a, uint32_t mul,
                                       uint32_t divisor)
{
	union {
		uint64_t ll;
		struct {
#ifdef __BIG_ENDIAN
			uint32_t high, low;
#else
			uint32_t low, high;
#endif
		} l;
	} u, rl, rh;

	u.ll = a;
	rl.ll = mul_u32_u32(u.l.low, mul);
	rh.ll = mul_u32_u32(u.l.high, mul) + rl.l.high;

	/* Bits 32-63 of the result will be in rh.l.low. */
	rl.l.high = do_div(rh.ll, divisor);

	/* Bits 0-31 of the result will be in rl.l.low.	*/
	do_div(rl.ll, divisor);

	rl.l.high = rh.l.low;
	return rl.ll;
}
#endif /* mul_u64_u32_div */
