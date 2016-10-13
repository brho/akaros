/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Functions for updating the TPM state with the status of boot path.
 */

#ifndef VBOOT_REFERENCE_2TPM_BOOTMODE_H_
#define VBOOT_REFERENCE_2TPM_BOOTMODE_H_

#include "2api.h"

/**
 * Return digest indicating the boot state
 *
 * @param ctx		Vboot context
 * @return		Pointer to sha1 digest of size VB2_SHA1_DIGEST_SIZE
 */
const uint8_t *vb2_get_boot_state_digest(struct vb2_context *ctx);

#endif  /* VBOOT_REFERENCE_2TPM_BOOTMODE_H_ */
