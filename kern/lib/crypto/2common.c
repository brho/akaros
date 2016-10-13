/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Common functions between firmware and kernel verified boot.
 * (Firmware portion)
 */

#include "2sysincludes.h"
#include "2common.h"
#include "2rsa.h"
#include "2sha.h"

int vb2_safe_memcmp(const void *s1, const void *s2, size_t size)
{
	const unsigned char *us1 = s1;
	const unsigned char *us2 = s2;
	int result = 0;

	if (0 == size)
		return 0;

	/*
	 * Code snippet without data-dependent branch due to Nate Lawson
	 * (nate@root.org) of Root Labs.
	 */
	while (size--)
		result |= *us1++ ^ *us2++;

	return result != 0;
}

int vb2_align(uint8_t **ptr, uint32_t *size, uint32_t align, uint32_t want_size)
{
	uintptr_t p = (uintptr_t)*ptr;
	uintptr_t offs = p & (align - 1);

	if (offs) {
		offs = align - offs;

		if (*size < offs)
			return VB2_ERROR_ALIGN_BIGGER_THAN_SIZE;

		*ptr += offs;
		*size -= offs;
	}

	if (*size < want_size)
		return VB2_ERROR_ALIGN_SIZE;

	return VB2_SUCCESS;
}

void vb2_workbuf_init(struct vb2_workbuf *wb, uint8_t *buf, uint32_t size)
{
	wb->buf = buf;
	wb->size = size;

	/* Align the buffer so allocations will be aligned */
	if (vb2_align(&wb->buf, &wb->size, VB2_WORKBUF_ALIGN, 0))
		wb->size = 0;
}

/**
 * Round up a number to a multiple of VB2_WORKBUF_ALIGN
 *
 * @param v		Number to round up
 * @return The number, rounded up.
 */
static __inline uint32_t wb_round_up(uint32_t v)
{
	return (v + VB2_WORKBUF_ALIGN - 1) & ~(VB2_WORKBUF_ALIGN - 1);
}

void *vb2_workbuf_alloc(struct vb2_workbuf *wb, uint32_t size)
{
	uint8_t *ptr = wb->buf;

	/* Round up size to work buffer alignment */
	size = wb_round_up(size);

	if (size > wb->size)
		return NULL;

	wb->buf += size;
	wb->size -= size;

	return ptr;
}

void *vb2_workbuf_realloc(struct vb2_workbuf *wb,
			  uint32_t oldsize,
			  uint32_t newsize)
{
	/*
	 * Just free and allocate to update the size.  No need to move/copy
	 * memory, since the new pointer is guaranteed to be the same as the
	 * old one.  The new allocation can fail, if the new size is too big.
	 */
	vb2_workbuf_free(wb, oldsize);
	return vb2_workbuf_alloc(wb, newsize);
}

void vb2_workbuf_free(struct vb2_workbuf *wb, uint32_t size)
{
	/* Round up size to work buffer alignment */
	size = wb_round_up(size);

	wb->buf -= size;
	wb->size += size;
}

ptrdiff_t vb2_offset_of(const void *base, const void *ptr)
{
	return (uintptr_t)ptr - (uintptr_t)base;
}
