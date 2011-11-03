#ifndef PARLIB_ARCH_BITMASK_H
#define PARLIB_ARCH_BITMASK_H

#include <string.h>
#include <sys/types.h>
#include <sys/param.h>
#include <arch/atomic.h>
#include <stdio.h>

#define DECL_BITMASK(name, size) \
	unsigned long (name)[WORDS_FOR_BITMASK((size))]

#define BPW (CHAR_BIT*sizeof(long))
#define WORDS_FOR_BITMASK(size) (((size)-1) / BPW + 1)
#define BYTES_FOR_BITMASK(size) (WORDS_FOR_BITMASK * sizeof(long))

#define BYTES_FOR_BITMASK_WITH_CHECK(size) \
	((size) ? BYTES_FOR_BITMASK(size) : (0))

static bool GET_BITMASK_BIT(unsigned long* name, size_t bit) 
{
	return (((name)[(bit)/BPW] & (1 << ((bit) % BPW))) ? 1 : 0);
}

#define SET_BITMASK_BIT(name, bit) \
	((name)[(bit)/BPW] |= (1 << ((bit) % BPW)));

#define CLR_BITMASK_BIT(name, bit) \
	((name)[(bit)/BPW] &= ~(1 << ((bit) % BPW)));

static void SET_BITMASK_BIT_ATOMIC(unsigned long* name, size_t bit) 
{
	__sync_fetch_and_or(&name[bit/BPW], bit % BPW);
}

#define CLR_BITMASK_BIT_ATOMIC(name, bit) \
	(__sync_fetch_and_and(&(name)[(bit)/BPW], ~(1UL << ((bit) % BPW))))

#define CLR_BITMASK(name, size) \
({ \
	{TRUSTEDBLOCK \
	memset((void*)((uintptr_t)(name)), 0, BYTES_FOR_BITMASK((size))); \
	} \
})

#define FILL_BITMASK(name, size) \
({ \
	{TRUSTEDBLOCK \
	memset((void*)((uintptr_t)(name)), 255, BYTES_FOR_BITMASK((size))); \
	} \
	if ((size) % BPW) \
	  (name)[WORDS_FOR_BITMASK((size))-1] >>= BPW - ((size) % BPW); \
}) 

#define COPY_BITMASK(newmask, oldmask, size) \
({ \
	{TRUSTEDBLOCK \
	memcpy((void*)((uintptr_t)(newmask)), \
           (void*)((uintptr_t)(oldmask)), \
           BYTES_FOR_BITMASK((size))); \
	} \
})

// this checks the entire last byte, so keep it 0 in the other macros
#define BITMASK_IS_CLEAR(name, size) ({ \
	size_t __n = WORDS_FOR_BITMASK(size); \
	unsigned long clear = 1; \
	while (clear != 0 && __n-- > 0) \
		clear = (name)[__n]; \
	clear != 0; })

static inline bool BITMASK_IS_FULL(unsigned long* map, size_t size)
{
	size_t extra = size % BPW;
	for (size_t i = 0; i < WORDS_FOR_BITMASK(size) - (extra != 0); i++)
	  if (map[i] != ~0UL)
		return FALSE;
	
	return !extra || map[WORDS_FOR_BITMASK(size)-1] == ((1UL << extra) - 1);
}

#define PRINT_BITMASK(name, size) { \
	int i;	\
	int _size = size; \
	for (i = 0; i < WORDS_FOR_BITMASK(size); i++) { \
		int j;	\
		for (j = 0; j < MIN(BPW,_size); j++) \
			printf("%x", ((name)[i] >> j) & 1);	\
			_size--; \
	} \
	printf("\n"); \
}

#endif /* PARLIB_ARCH_BITMASK_H */
