/* Fast hashing routine for ints,  longs and pointers.
   (C) 2002 Nadia Yvette Chambers, IBM */

/* This file came from Linux, 4.6.
 * This source code is licensed under the GNU General Public License
 * Version 2. See the file COPYING for more details. */

#pragma once

/*
 * Knuth recommends primes in approximately golden ratio to the maximum
 * integer representable by a machine word for multiplicative hashing.
 * Chuck Lever verified the effectiveness of this technique:
 * http://www.citi.umich.edu/techreports/reports/citi-tr-00-1.pdf
 *
 * These primes are chosen to be bit-sparse, that is operations on
 * them can use shifts and additions instead of multiplications for
 * machines where multiplications are slow.
 */

#include <arch/types.h>
#include <compiler.h>

/* 2^31 + 2^29 - 2^25 + 2^22 - 2^19 - 2^16 + 1 */
#define GOLDEN_RATIO_PRIME_32 0x9e370001UL
/*  2^63 + 2^61 - 2^57 + 2^54 - 2^51 - 2^18 + 1 */
#define GOLDEN_RATIO_PRIME_64 0x9e37fffffffc0001UL

#if BITS_PER_LONG == 32
#define GOLDEN_RATIO_PRIME GOLDEN_RATIO_PRIME_32
#define hash_long(val, bits) hash_32(val, bits)
#elif BITS_PER_LONG == 64
#define hash_long(val, bits) hash_64(val, bits)
#define GOLDEN_RATIO_PRIME GOLDEN_RATIO_PRIME_64
#else
#error Wordsize not 32 or 64
#endif

/*
 * The above primes are actively bad for hashing, since they are
 * too sparse. The 32-bit one is mostly ok, the 64-bit one causes
 * real problems. Besides, the "prime" part is pointless for the
 * multiplicative hash.
 *
 * Although a random odd number will do, it turns out that the golden
 * ratio phi = (sqrt(5)-1)/2, or its negative, has particularly nice
 * properties.
 *
 * These are the negative, (1 - phi) = (phi^2) = (3 - sqrt(5))/2.
 * (See Knuth vol 3, section 6.4, exercise 9.)
 */
#define GOLDEN_RATIO_32 0x61C88647
#define GOLDEN_RATIO_64 0x61C8864680B583EBull

static __always_inline uint64_t hash_64(uint64_t val, unsigned int bits)
{
	uint64_t hash = val;

#if BITS_PER_LONG == 64
	hash = hash * GOLDEN_RATIO_64;
#else
	/*  Sigh, gcc can't optimise this alone like it does for 32 bits. */
	uint64_t n = hash;
	n <<= 18;
	hash -= n;
	n <<= 33;
	hash -= n;
	n <<= 3;
	hash += n;
	n <<= 3;
	hash -= n;
	n <<= 4;
	hash += n;
	n <<= 2;
	hash += n;
#endif

	/* High bits are more random, so use them. */
	return hash >> (64 - bits);
}

static inline uint32_t hash_32(uint32_t val, unsigned int bits)
{
	/* On some cpus multiply is faster, on others gcc will do shifts */
	uint32_t hash = val * GOLDEN_RATIO_PRIME_32;

	/* High bits are more random, so use them. */
	return hash >> (32 - bits);
}

static inline unsigned long hash_ptr(const void *ptr, unsigned int bits)
{
	return hash_long((unsigned long)ptr, bits);
}

static inline uint32_t hash32_ptr(const void *ptr)
{
	unsigned long val = (unsigned long)ptr;

#if BITS_PER_LONG == 64
	val ^= (val >> 32);
#endif
	return (uint32_t)val;
}
