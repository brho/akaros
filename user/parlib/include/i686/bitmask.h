#ifndef PARLIB_ARCH_BITMASK_H
#define PARLIB_ARCH_BITMASK_H

#include <string.h>
#include <sys/types.h>
#include <sys/param.h>
#include <arch/atomic.h>
#include <stdio.h>

#define DECL_BITMASK(name, size) \
	uint8_t (name)[BYTES_FOR_BITMASK((size))]

#define BYTES_FOR_BITMASK(size) \
	(((size) - 1) / 8 + 1)

#define BYTES_FOR_BITMASK_WITH_CHECK(size) \
	((size) ? ((size) - (1)) / (8) + (1) : (0))

static bool GET_BITMASK_BIT(uint8_t* name, size_t bit) 
{
	return (((name)[(bit)/8] & (1 << ((bit) % 8))) ? 1 : 0);
}

#define SET_BITMASK_BIT(name, bit) \
	((name)[(bit)/8] |= (1 << ((bit) % 8)));
/*
static void SET_BITMASK_BIT(uint8_t* name, size_t bit)
{
	((name)[(bit)/8] |= (1 << ((bit) % 8)));
}
*/

#define CLR_BITMASK_BIT(name, bit) \
	((name)[(bit)/8] &= ~(1 << ((bit) % 8)));
/*
static void CLR_BITMASK_BIT(uint8_t* name, size_t bit) 
{
	((name)[(bit)/8] &= ~(1 << ((bit) % 8)));
}
*/

static void SET_BITMASK_BIT_ATOMIC(uint8_t* name, size_t bit) 
{
	(atomic_orb(&(name)[(bit)/8], (1 << ((bit) % 8))));
}

#define CLR_BITMASK_BIT_ATOMIC(name, bit) \
	(atomic_andb(&(name)[(bit)/8], ~(1 << ((bit) % 8))))

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
	(name)[BYTES_FOR_BITMASK((size))-1] >>= (((size) % 8) ? (8 - ((size) % 8)) : 0 ); \
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
	uint32_t __n = BYTES_FOR_BITMASK((size)); \
	bool clear = 1; \
	while (__n-- > 0) { \
		if ((name)[__n]) { \
			clear = 0; \
			break;\
		}\
	} \
	clear; })

static inline bool BITMASK_IS_FULL(uint8_t* map, size_t size)
{
	int _size = size;
	for (int i = 0; i < BYTES_FOR_BITMASK(size); i++) {
		for (int j = 0; j < MIN(8,_size); j++)
			if(!((map[i] >> j) &1))
				return FALSE;
			_size--;
	}
	return TRUE;
}

#define PRINT_BITMASK(name, size) { \
	int i;	\
	int _size = size; \
	for (i = 0; i < BYTES_FOR_BITMASK(size); i++) { \
		int j;	\
		for (j = 0; j < MIN(8,_size); j++) \
			printk("%x", ((name)[i] >> j) & 1);	\
			_size--; \
	} \
	printk("\n"); \
}

#endif /* PARLIB_ARCH_BITMASK_H */
