#pragma once

#include <string.h>
#include <sys/types.h>
#include <sys/param.h>
#include <parlib/arch/atomic.h>
#include <stdio.h>

__BEGIN_DECLS

#define DECL_BITMASK(name, size) \
	uint8_t (name)[BYTES_FOR_BITMASK((size))]

#define BYTES_FOR_BITMASK(size) \
	(((size) - 1) / 8 + 1)

static inline bool GET_BITMASK_BIT(const uint8_t *name, size_t bit)
{
	return name[bit / 8] & (1 << (bit % 8)) ? TRUE : FALSE;
}

static inline void SET_BITMASK_BIT(uint8_t *name, size_t bit)
{
	name[bit / 8] |= 1 << (bit % 8);
}

static inline void CLR_BITMASK_BIT(uint8_t *name, size_t bit)
{
	name[bit / 8] &= ~(1 << (bit % 8));
}

static inline void SET_BITMASK_BIT_ATOMIC(uint8_t *name, size_t bit)
{
	atomic_orb(&name[bit / 8], 1 << (bit % 8));
}

static inline void CLR_BITMASK_BIT_ATOMIC(uint8_t *name, size_t bit)
{
	atomic_andb(&name[bit / 8], ~(1 << (bit % 8)));
}

static inline void CLR_BITMASK(uint8_t *name, size_t size)
{
	memset(name, 0, BYTES_FOR_BITMASK(size));
}

static inline void FILL_BITMASK(uint8_t *name, size_t size)
{
	memset(name, 255, BYTES_FOR_BITMASK(size));
	name[BYTES_FOR_BITMASK(size) - 1] >>= ((size % 8) ? (8 - (size % 8)) : 0);
}

static inline void COPY_BITMASK(uint8_t *newmask, const uint8_t *oldmask,
                                size_t size)
{
	memcpy(newmask, oldmask, BYTES_FOR_BITMASK(size));
}

/* this checks the entire last byte, so keep it 0 in the other functions */
static bool BITMASK_IS_CLEAR(const uint8_t *name, size_t size)
{
	uint32_t __n = BYTES_FOR_BITMASK(size);
	bool clear = TRUE;

	while (__n-- > 0) {
		if (name[__n]) {
			clear = FALSE;
			break;
		}
	}
	return clear;
}

static inline bool BITMASK_IS_FULL(const uint8_t *map, size_t size)
{
	size_t nr_bytes = BYTES_FOR_BITMASK(size);

	for (int i = 0; i < nr_bytes; i++) {
		for (int j = 0; j < MIN(8, size); j++) {
			if (!((map[i] >> j) & 1))
				return FALSE;
			size--;
		}
	}
	return TRUE;
}

static inline void PRINT_BITMASK(const uint8_t *name, size_t size)
{
	size_t nr_bytes = BYTES_FOR_BITMASK(size);

	for (int i = 0; i < nr_bytes; i++) {
		for (int j = 0; j < MIN(8, size); j++) {
			printf("%x", (name[i] >> j) & 1);
			size--;
		}
	}
	printf("\n");
}

__END_DECLS
