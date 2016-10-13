/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Crypto constants for verified boot
 */

#ifndef VBOOT_REFERENCE_VBOOT_2CRYPTO_H_
#define VBOOT_REFERENCE_VBOOT_2CRYPTO_H_
#include <stdint.h>

/* Verified boot crypto algorithms */
enum vb2_crypto_algorithm {
	VB2_ALG_RSA1024_SHA1   = 0,
	VB2_ALG_RSA1024_SHA256 = 1,
	VB2_ALG_RSA1024_SHA512 = 2,
	VB2_ALG_RSA2048_SHA1   = 3,
	VB2_ALG_RSA2048_SHA256 = 4,
	VB2_ALG_RSA2048_SHA512 = 5,
	VB2_ALG_RSA4096_SHA1   = 6,
	VB2_ALG_RSA4096_SHA256 = 7,
	VB2_ALG_RSA4096_SHA512 = 8,
	VB2_ALG_RSA8192_SHA1   = 9,
	VB2_ALG_RSA8192_SHA256 = 10,
	VB2_ALG_RSA8192_SHA512 = 11,

	/* Number of algorithms */
	VB2_ALG_COUNT
};

/* Algorithm types for signatures */
enum vb2_signature_algorithm {
	/* Invalid or unsupported signature type */
	VB2_SIG_INVALID = 0,

	/*
	 * No signature algorithm.  The digest is unsigned.  See
	 * VB2_ID_NONE_* for key IDs to use with this algorithm.
	 */
	VB2_SIG_NONE = 1,

	/* RSA algorithms of the given length in bits (1024-8192) */
	VB2_SIG_RSA1024 = 2,  /* Warning!  This is likely to be deprecated! */
	VB2_SIG_RSA2048 = 3,
	VB2_SIG_RSA4096 = 4,
	VB2_SIG_RSA8192 = 5,
};

/* Algorithm types for hash digests */
enum vb2_hash_algorithm {
	/* Invalid or unsupported digest type */
	VB2_HASH_INVALID = 0,

	/* SHA-1.  Warning: This is likely to be deprecated soon! */
	VB2_HASH_SHA1 = 1,

	/* SHA-256 and SHA-512 */
	VB2_HASH_SHA256 = 2,
	VB2_HASH_SHA512 = 3,

	/* Last index. Don't add anything below. */
	VB2_HASH_ALG_COUNT,
};

#endif /* VBOOT_REFERENCE_VBOOT_2CRYPTO_H_ */
