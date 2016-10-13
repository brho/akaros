/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Common functions between firmware and kernel verified boot.
 */

#ifndef VBOOT_REFERENCE_VBOOT_2COMMON_H_
#define VBOOT_REFERENCE_VBOOT_2COMMON_H_

#include "2api.h"
#include "2return_codes.h"
#include "2sha.h"
#include "2struct.h"

struct vb2_public_key;

/*
 * Return the greater of A and B.  This is used in macros which calculate the
 * required buffer size, so can't be turned into a static inline function.
 */
#ifndef VB2_MAX
#define VB2_MAX(A, B) ((A) > (B) ? (A) : (B))
#endif

/* Return the number of elements in an array */
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(array) (sizeof(array)/sizeof(array[0]))
#endif

/* Debug output printf() for tests.  Otherwise, it's platform-dependent. */
#if defined(VBOOT_DEBUG)
#  if defined(FOR_TEST)
#    define VB2_DEBUG(format, args...) printf(format, ## args)
#  else
#    define VB2_DEBUG(format, args...) vb2ex_printf(__func__, format, ## args)
#  endif
#else
#  define VB2_DEBUG(format, args...)
#endif

/*
 * Alignment for work buffer pointers/allocations should be useful for any
 * data type. When declaring workbuf buffers on the stack, the caller should
 * use explicit alignment to avoid run-time errors. For example:
 *
 *    int foo(void)
 *    {
 *        struct vb2_workbuf wb;
 *        uint8_t buf[NUM] __attribute__ ((aligned (VB2_WORKBUF_ALIGN)));
 *        wb.buf = buf;
 *        wb.size = sizeof(buf);
 */

/* We might get away with using __alignof__(void *), but since GCC defines a
 * macro for us we'll be safe and use that. */
#define VB2_WORKBUF_ALIGN __BIGGEST_ALIGNMENT__

/* Work buffer */
struct vb2_workbuf {
	uint8_t *buf;
	uint32_t size;
};

/**
 * Initialize a work buffer.
 *
 * @param wb		Work buffer to init
 * @param buf		Pointer to work buffer data
 * @param size		Size of work buffer data in bytes
 */
void vb2_workbuf_init(struct vb2_workbuf *wb, uint8_t *buf, uint32_t size);

/**
 * Allocate space in a work buffer.
 *
 * Note that the returned buffer will always be aligned to VB2_WORKBUF_ALIGN.
 *
 * The work buffer acts like a stack, and detailed tracking of allocs and frees
 * is not done.  The caller must track the size of each allocation and free via
 * vb2_workbuf_free() in the reverse order they were allocated.
 *
 * An acceptable alternate workflow inside a function is to pass in a const
 * work buffer, then make a local copy.  Allocations done to the local copy
 * then don't change the passed-in work buffer, and will effectively be freed
 * when the local copy goes out of scope.
 *
 * @param wb		Work buffer
 * @param size		Requested size in bytes
 * @return A pointer to the allocated space, or NULL if error.
 */
void *vb2_workbuf_alloc(struct vb2_workbuf *wb, uint32_t size);

/**
 * Reallocate space in a work buffer.
 *
 * Note that the returned buffer will always be aligned to VB2_WORKBUF_ALIGN.
 * The work buffer acts like a stack, so this must only be done to the most
 * recently allocated buffer.
 *
 * @param wb		Work buffer
 * @param oldsize	Old allocation size in bytes
 * @param newsize	Requested size in bytes
 * @return A pointer to the allocated space, or NULL if error.
 */
void *vb2_workbuf_realloc(struct vb2_workbuf *wb,
			  uint32_t oldsize,
			  uint32_t newsize);

/**
 * Free the preceding allocation.
 *
 * Note that the work buffer acts like a stack, and detailed tracking of
 * allocs and frees is not done.  The caller must track the size of each
 * allocation and free them in reverse order.
 *
 * @param wb		Work buffer
 * @param size		Size of data to free
 */
void vb2_workbuf_free(struct vb2_workbuf *wb, uint32_t size);

/* Check if a pointer is aligned on an align-byte boundary */
#define vb2_aligned(ptr, align) (!(((uintptr_t)(ptr)) & ((align) - 1)))

/**
 * Safer memcmp() for use in crypto.
 *
 * Compares the buffers to see if they are equal.  Time taken to perform
 * the comparison is dependent only on the size, not the relationship of
 * the match between the buffers.  Note that unlike memcmp(), this only
 * indicates inequality, not which buffer is lesser.
 *
 * @param s1		First buffer
 * @param s2		Second buffer
 * @param size		Number of bytes to compare
 * @return 0 if match or size=0, non-zero if at least one byte mismatched.
 */
int vb2_safe_memcmp(const void *s1, const void *s2, size_t size);

/**
 * Align a buffer and check its size.
 *
 * @param **ptr		Pointer to pointer to align
 * @param *size		Points to size of buffer pointed to by *ptr
 * @param align		Required alignment (must be power of 2)
 * @param want_size	Required size
 * @return VB2_SUCCESS, or non-zero if error.
 */
int vb2_align(uint8_t **ptr,
	      uint32_t *size,
	      uint32_t align,
	      uint32_t want_size);

/**
 * Return offset of ptr from base.
 *
 * @param base		Base pointer
 * @param ptr		Pointer at some offset from base
 * @return The offset of ptr from base.
 */
ptrdiff_t vb2_offset_of(const void *base, const void *ptr);

/**
 * Return expected signature size for a signature/hash algorithm pair
 *
 * @param sig_alg	Signature algorithm
 * @param hash_alg	Hash algorithm
 * @return The signature size, or zero if error / unsupported algorithm.
 */
uint32_t vb2_sig_size(enum vb2_signature_algorithm sig_alg,
		      enum vb2_hash_algorithm hash_alg);

/**
 * Return a key ID for an unsigned hash algorithm.
 *
 * @param hash_alg	Hash algorithm to return key for
 * @return A pointer to the key ID for that hash algorithm with
 *	   sig_alg=VB2_SIG_NONE, or NULL if error.
 */
const struct vb2_id *vb2_hash_id(enum vb2_hash_algorithm hash_alg);

/* Size of work buffer sufficient for vb2_verify_digest() worst case. */
#define VB2_VERIFY_DIGEST_WORKBUF_BYTES VB2_VERIFY_RSA_DIGEST_WORKBUF_BYTES

/* Size of work buffer sufficient for vb2_verify_data() worst case. */
#define VB2_VERIFY_DATA_WORKBUF_BYTES					\
	(VB2_SHA512_DIGEST_SIZE +					\
	 VB2_MAX(VB2_VERIFY_DIGEST_WORKBUF_BYTES,			\
		 sizeof(struct vb2_digest_context)))

/* Size of work buffer sufficient for vb2_verify_keyblock() worst case. */
#define VB2_KEY_BLOCK_VERIFY_WORKBUF_BYTES VB2_VERIFY_DATA_WORKBUF_BYTES

/* Size of work buffer sufficient for vb2_verify_fw_preamble() worst case. */
#define VB2_VERIFY_FIRMWARE_PREAMBLE_WORKBUF_BYTES VB2_VERIFY_DATA_WORKBUF_BYTES

#endif  /* VBOOT_REFERENCE_VBOOT_2COMMON_H_ */
