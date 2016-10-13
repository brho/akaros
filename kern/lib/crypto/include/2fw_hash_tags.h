/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Firmware hash tags for verified boot
 */

#ifndef VBOOT_REFERENCE_VBOOT_2FW_HASH_TAGS_H_
#define VBOOT_REFERENCE_VBOOT_2FW_HASH_TAGS_H_
#include <stdint.h>

/*
 * Tags for types of hashable data.
 *
 * Note that not every firmware image will contain every tag.
 *
 * TODO: These are the ones that vboot specifically knows about given the
 * current data structures.  In the future, I'd really like the vboot preamble
 * to contain an arbitrary list of tags and their hashes, so that we can hash
 * ram init, main RW body, EC-RW for software sync, etc. all separately.
 */
enum vb2_hash_tag {
	/* Invalid hash tag; never present in table */
	VB2_HASH_TAG_INVALID = 0,

	/* Firmware body */
	VB2_HASH_TAG_FW_BODY = 1,

	/* Kernel data key */
	VB2_HASH_TAG_KERNEL_DATA_KEY = 2,

	/*
	 * Tags over 0x40000000 are reserved for use by the calling firmware,
	 * which may associate them with arbitrary types of RW firmware data
	 * that it wants to track.
	 */
	VB2_HASH_TAG_CALLER_BASE = 0x40000000
};

#endif /* VBOOT_REFERENCE_VBOOT_2FW_HASH_TAGS_H_ */
