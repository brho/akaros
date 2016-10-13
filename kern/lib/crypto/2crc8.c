/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "2sysincludes.h"
#include "2crc8.h"

uint8_t vb2_crc8(const void *vptr, uint32_t size)
{
	const uint8_t *data = vptr;
	unsigned crc = 0;
	uint32_t i, j;

	/*
	 * Calculate CRC-8 directly.  A table-based algorithm would be faster,
	 * but for only a few bytes it isn't worth the code size.
	 */
	for (j = size; j; j--, data++) {
		crc ^= (*data << 8);
		for(i = 8; i; i--) {
			if (crc & 0x8000)
				crc ^= (0x1070 << 3);
			crc <<= 1;
		}
	}

	return (uint8_t)(crc >> 8);
}
