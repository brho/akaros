/* Copyright (c) 2015 Google Inc
 * Davide Libenzi <dlibenzi@google.com>
 * See LICENSE for details.
 */

#pragma once

#include <sys/types.h>
#include <stdint.h>

static inline const void *get_le_u16(const void *ptr, uint16_t *pvalue)
{
	const uint8_t *p = (const uint8_t *) ptr;

	*pvalue = (uint16_t) p[0] | ((uint16_t) p[1] << 8);
	return p + sizeof(uint16_t);
}

static inline const void *get_le_u32(const void *ptr, uint32_t *pvalue)
{
	const uint8_t *p = (const uint8_t *) ptr;

	*pvalue = (uint32_t) p[0] | ((uint32_t) p[1] << 8) |
		((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);
	return p + sizeof(uint32_t);
}

static inline const void *get_le_u64(const void *ptr, uint64_t *pvalue)
{
	const uint8_t *p = (const uint8_t *) ptr;

	*pvalue = (uint64_t) p[0] | ((uint64_t) p[1] << 8) |
		((uint64_t) p[2] << 16) | ((uint64_t) p[3] << 24) |
		((uint64_t) p[4] << 32) | ((uint64_t) p[5] << 40) |
		((uint64_t) p[6] << 48) | ((uint64_t) p[7] << 56);
	return p + sizeof(uint64_t);
}

static inline void *put_le_u16(void *ptr, uint16_t v)
{
	uint8_t *p = (uint8_t *) ptr;

	p[0] = (uint8_t) v;
	p[1] = (uint8_t) (v >> 8);
	return p + sizeof(uint16_t);
}

static inline void *put_le_u32(void *ptr, uint32_t v)
{
	uint8_t *p = (uint8_t *) ptr;

	p[0] = (uint8_t) v;
	p[1] = (uint8_t) (v >> 8);
	p[2] = (uint8_t) (v >> 16);
	p[3] = (uint8_t) (v >> 24);
	return p + sizeof(uint32_t);
}

static inline void *put_le_u64(void *ptr, uint64_t v)
{
	uint8_t *p = (uint8_t *) ptr;

	p[0] = (uint8_t) v;
	p[1] = (uint8_t) (v >> 8);
	p[2] = (uint8_t) (v >> 16);
	p[3] = (uint8_t) (v >> 24);
	p[4] = (uint8_t) (v >> 32);
	p[5] = (uint8_t) (v >> 40);
	p[6] = (uint8_t) (v >> 48);
	p[7] = (uint8_t) (v >> 56);
	return p + sizeof(uint64_t);
}
