/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef VBOOT_REFERENCE_VBOOT_2HMAC_H_
#define VBOOT_REFERENCE_VBOOT_2HMAC_H_

#include <stdint.h>
#include "2crypto.h"

/**
 * Compute HMAC
 *
 * @param alg		Hash algorithm ID
 * @param key		HMAC key
 * @param key_size	HMAC key size
 * @param msg		Message to compute HMAC for
 * @param msg_size	Message size
 * @param mac		Computed message authentication code
 * @param mac_size	Size of the buffer pointed by <mac>
 * @return
 */
int hmac(enum vb2_hash_algorithm alg,
	 const void *key, uint32_t key_size,
	 const void *msg, uint32_t msg_size,
	 uint8_t *mac, uint32_t mac_size);

#endif
