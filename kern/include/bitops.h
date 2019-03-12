#pragma once

#define BIT(nr)			(1UL << (nr))
#define BIT_MASK(nr)		(1UL << ((nr) % BITS_PER_LONG))
#define BIT_WORD(nr)		((nr) / BITS_PER_LONG)
#define BITS_PER_BYTE		8
#define BITS_TO_LONGS(nr)	DIV_ROUND_UP(nr, BITS_PER_BYTE * sizeof(long))

extern unsigned int __sw_hweight8(unsigned int w);
extern unsigned int __sw_hweight16(unsigned int w);
extern unsigned int __sw_hweight32(unsigned int w);
extern unsigned long __sw_hweight64(uint64_t w);

/* not clear where we want these defined. */
unsigned long find_last_bit(const unsigned long *addr, unsigned long size);
unsigned long find_next_bit(const unsigned long *addr, unsigned long size,
			    unsigned long offset);
unsigned long find_next_zero_bit(const unsigned long *addr, unsigned long size,
				 unsigned long offset);
unsigned long find_first_bit(const unsigned long *addr, unsigned long size);
unsigned long find_first_zero_bit(const unsigned long *addr, unsigned long size);

#include <arch/bitops.h>

#define for_each_set_bit(bit, addr, size) \
	for ((bit) = find_first_bit((addr), (size));		\
	     (bit) < (size);					\
	     (bit) = find_next_bit((addr), (size), (bit) + 1))

/* same as for_each_set_bit() but use bit as value to start with */
#define for_each_set_bit_from(bit, addr, size) \
	for ((bit) = find_next_bit((addr), (size), (bit));	\
	     (bit) < (size);					\
	     (bit) = find_next_bit((addr), (size), (bit) + 1))

#define for_each_clear_bit(bit, addr, size) \
	for ((bit) = find_first_zero_bit((addr), (size));	\
	     (bit) < (size);					\
	     (bit) = find_next_zero_bit((addr), (size), (bit) + 1))

/* same as for_each_clear_bit() but use bit as value to start with */
#define for_each_clear_bit_from(bit, addr, size) \
	for ((bit) = find_next_zero_bit((addr), (size), (bit));	\
	     (bit) < (size);					\
	     (bit) = find_next_zero_bit((addr), (size), (bit) + 1))

static __inline__ int get_bitmask_order(unsigned int count)
{
	int order;

	order = fls(count);
	return order;	/* We could be slightly more clever with -1 here... */
}

static __inline__ int get_count_order(unsigned int count)
{
	int order;

	order = fls(count) - 1;
	if (count & (count - 1))
		order++;
	return order;
}

static inline unsigned long hweight_long(unsigned long w)
{
	return __builtin_popcount(w);
	//return sizeof(w) == 4 ? hweight32(w) : hweight64(w);
}

static inline unsigned fls_long(unsigned long l)
{
	if (sizeof(l) == 4)
		return fls(l);
	return fls64(l);
}

// not used yet and I have other things I'm trying to do
#if 0
static inline bool BITMASK_IS_SET_IN_RANGE(uint8_t* m, size_t beg, size_t end)
{
	for(size_t i=beg; i<end; i++) {
		if(!GET_BITMASK_BIT(m, i))
			return FALSE;
	}
	return TRUE;
}

static inline bool BITMASK_IS_CLR_IN_RANGE(uint8_t* m, size_t beg, size_t end)
{
	for(size_t i=beg; i<end; i++) {
		if(GET_BITMASK_BIT(m, i))
			return FALSE;
	}
	return TRUE;
}

static inline void SET_BITMASK_RANGE(uint8_t* m, size_t beg, size_t end)
{
	for(size_t i=beg; i<end; i++) {
		SET_BITMASK_BIT(m, i);
	}
}

static inline void CLR_BITMASK_RANGE(uint8_t* m, size_t beg, size_t end)
{
	for(size_t i=beg; i<end; i++) {
		CLR_BITMASK_BIT(m, i);
	}
}


/* Runs *work on every bit in the bitmask, passing *work the value of the bit
 * that is set.  Optionally clears the bit from the bitmask.
 *
 * We need this to be a macro, so that the calling code doesn't need the
 * address for work_fn.  This matters for code that has nested functions that
 * use local variables, since taking the address of work_fn will force the
 * compiler to put the function on the stack and incur icache coherency
 * overheads. */
#define BITMASK_FOREACH_SET(name, size, work_fn, clear)                        \
{                                                                              \
	for (int i = 0; i < (size); i++) {                                     \
		bool present = GET_BITMASK_BIT((name), i);                     \
		if (present && (clear))                                        \
			CLR_BITMASK_BIT_ATOMIC((name), i);                     \
		if (present)                                                   \
			(work_fn)(i);                                          \
	}                                                                      \
}
#endif
