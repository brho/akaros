/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Very simple 8-bit CRC function.
 */

#ifndef VBOOT_REFERENCE_2_CRC8_H_
#define VBOOT_REFERENCE_2_CRC8_H_

/**
 * Calculate CRC-8 of the data, using x^8 + x^2 + x + 1 polynomial.
 *
 * @param data		Data to CRC
 * @param size		Size of data in bytes
 * @return CRC-8 of the data.
 */
uint8_t vb2_crc8(const void *data, uint32_t size);

#endif /* VBOOT_REFERENCE_2_CRC8_H_ */
