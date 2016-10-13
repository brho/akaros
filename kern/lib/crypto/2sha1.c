/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * SHA-1 implementation largely based on libmincrypt in the the Android
 * Open Source Project (platorm/system/core.git/libmincrypt/sha.c
 */

#include "2sysincludes.h"
#include "2common.h"
#include "2sha.h"

/*
 * Some machines lack byteswap.h and endian.h. These have to use the
 * slower code, even if they're little-endian.
 */

#if defined(HAVE_ENDIAN_H) && defined(HAVE_LITTLE_ENDIAN)

/*
 * This version is about 28% faster than the generic version below,
 * but assumes little-endianness.
 */
static uint32_t ror27(uint32_t val)
{
	return (val >> 27) | (val << 5);
}

static uint32_t ror2(uint32_t val)
{
	return (val >> 2) | (val << 30);
}

static uint32_t ror31(uint32_t val)
{
	return (val >> 31) | (val << 1);
}

static void sha1_transform(struct vb2_sha1_context *ctx)
{
	/* Note that this array uses 80*4=320 bytes of stack */
	uint32_t W[80];
	register uint32_t A, B, C, D, E;
	int t;

	A = ctx->state[0];
	B = ctx->state[1];
	C = ctx->state[2];
	D = ctx->state[3];
	E = ctx->state[4];

#define SHA_F1(A,B,C,D,E,t)				\
	E += ror27(A) +					\
		(W[t] = bswap_32(ctx->buf.w[t])) +	\
		(D^(B&(C^D))) + 0x5A827999;		\
	B = ror2(B);

	for (t = 0; t < 15; t += 5) {
		SHA_F1(A,B,C,D,E,t + 0);
		SHA_F1(E,A,B,C,D,t + 1);
		SHA_F1(D,E,A,B,C,t + 2);
		SHA_F1(C,D,E,A,B,t + 3);
		SHA_F1(B,C,D,E,A,t + 4);
	}
	SHA_F1(A,B,C,D,E,t + 0);  /* 16th one, t == 15 */

#undef SHA_F1

#define SHA_F1(A,B,C,D,E,t)						\
	E += ror27(A) +							\
		(W[t] = ror31(W[t-3] ^ W[t-8] ^ W[t-14] ^ W[t-16])) +	\
		(D^(B&(C^D))) + 0x5A827999;				\
	B = ror2(B);

	SHA_F1(E,A,B,C,D,t + 1);
	SHA_F1(D,E,A,B,C,t + 2);
	SHA_F1(C,D,E,A,B,t + 3);
	SHA_F1(B,C,D,E,A,t + 4);

#undef SHA_F1

#define SHA_F2(A,B,C,D,E,t)						\
	E += ror27(A) +							\
		(W[t] = ror31(W[t-3] ^ W[t-8] ^ W[t-14] ^ W[t-16])) +	\
		(B^C^D) + 0x6ED9EBA1;					\
	B = ror2(B);

	for (t = 20; t < 40; t += 5) {
		SHA_F2(A,B,C,D,E,t + 0);
		SHA_F2(E,A,B,C,D,t + 1);
		SHA_F2(D,E,A,B,C,t + 2);
		SHA_F2(C,D,E,A,B,t + 3);
		SHA_F2(B,C,D,E,A,t + 4);
	}

#undef SHA_F2

#define SHA_F3(A,B,C,D,E,t)						\
	E += ror27(A) +							\
		(W[t] = ror31(W[t-3] ^ W[t-8] ^ W[t-14] ^ W[t-16])) +	\
		((B&C)|(D&(B|C))) + 0x8F1BBCDC;				\
	B = ror2(B);

	for (; t < 60; t += 5) {
		SHA_F3(A,B,C,D,E,t + 0);
		SHA_F3(E,A,B,C,D,t + 1);
		SHA_F3(D,E,A,B,C,t + 2);
		SHA_F3(C,D,E,A,B,t + 3);
		SHA_F3(B,C,D,E,A,t + 4);
	}

#undef SHA_F3

#define SHA_F4(A,B,C,D,E,t)						\
	E += ror27(A) +							\
		(W[t] = ror31(W[t-3] ^ W[t-8] ^ W[t-14] ^ W[t-16])) +	\
		(B^C^D) + 0xCA62C1D6;					\
	B = ror2(B);

	for (; t < 80; t += 5) {
		SHA_F4(A,B,C,D,E,t + 0);
		SHA_F4(E,A,B,C,D,t + 1);
		SHA_F4(D,E,A,B,C,t + 2);
		SHA_F4(C,D,E,A,B,t + 3);
		SHA_F4(B,C,D,E,A,t + 4);
	}

#undef SHA_F4

	ctx->state[0] += A;
	ctx->state[1] += B;
	ctx->state[2] += C;
	ctx->state[3] += D;
	ctx->state[4] += E;
}

void vb2_sha1_update(struct vb2_sha1_context *ctx,
		     const uint8_t *data,
		     uint32_t size)
{
	int i = ctx->count % sizeof(ctx->buf);
	const uint8_t *p = (const uint8_t*)data;

	ctx->count += size;

	while (size > sizeof(ctx->buf) - i) {
		memcpy(&ctx->buf.b[i], p, sizeof(ctx->buf) - i);
		size -= sizeof(ctx->buf) - i;
		p += sizeof(ctx->buf) - i;
		sha1_transform(ctx);
		i = 0;
	}

	while (size--) {
		ctx->buf.b[i++] = *p++;
		if (i == sizeof(ctx->buf)) {
			sha1_transform(ctx);
			i = 0;
		}
	}
}

uint8_t *vb2_sha1_finalize(struct vb2_sha1_context *ctx)
{
	uint32_t cnt = ctx->count * 8;
	int i;

	vb2_sha1_update(ctx, (uint8_t*)"\x80", 1);
	while ((ctx->count % sizeof(ctx->buf)) != (sizeof(ctx->buf) - 8)) {
		vb2_sha1_update(ctx, (uint8_t*)"\0", 1);
	}

	for (i = 0; i < 8; ++i) {
		uint8_t tmp = cnt >> ((7 - i) * 8);
		vb2_sha1_update(ctx, &tmp, 1);
	}

	for (i = 0; i < 5; i++) {
		ctx->buf.w[i] = bswap_32(ctx->state[i]);
	}

	return ctx->buf.b;
}

#else   /* #if defined(HAVE_ENDIAN_H) && defined(HAVE_LITTLE_ENDIAN) */

#define rol(bits, value) (((value) << (bits)) | ((value) >> (32 - (bits))))

static void sha1_transform(struct vb2_sha1_context *ctx)
{
	/* Note that this array uses 80*4=320 bytes of stack */
	uint32_t W[80];
	uint32_t A, B, C, D, E;
	uint8_t *p = ctx->buf;
	int t;

	for(t = 0; t < 16; ++t) {
		uint32_t tmp = *p++ << 24;
		tmp |= *p++ << 16;
		tmp |= *p++ << 8;
		tmp |= *p++;
		W[t] = tmp;
	}

	for(; t < 80; t++) {
		W[t] = rol(1,W[t-3] ^ W[t-8] ^ W[t-14] ^ W[t-16]);
	}

	A = ctx->state[0];
	B = ctx->state[1];
	C = ctx->state[2];
	D = ctx->state[3];
	E = ctx->state[4];

	for(t = 0; t < 80; t++) {
		uint32_t tmp = rol(5,A) + E + W[t];

		if (t < 20)
			tmp += (D^(B&(C^D))) + 0x5A827999;
		else if ( t < 40)
			tmp += (B^C^D) + 0x6ED9EBA1;
		else if ( t < 60)
			tmp += ((B&C)|(D&(B|C))) + 0x8F1BBCDC;
		else
			tmp += (B^C^D) + 0xCA62C1D6;

		E = D;
		D = C;
		C = rol(30,B);
		B = A;
		A = tmp;
	}

	ctx->state[0] += A;
	ctx->state[1] += B;
	ctx->state[2] += C;
	ctx->state[3] += D;
	ctx->state[4] += E;
}

void vb2_sha1_update(struct vb2_sha1_context *ctx,
		     const uint8_t *data,
		     uint32_t size)
{
	int i = (int)(ctx->count % sizeof(ctx->buf));
	const uint8_t* p = (const uint8_t*) data;

	ctx->count += size;

	while (size--) {
		ctx->buf[i++] = *p++;
		if (i == sizeof(ctx->buf)) {
			sha1_transform(ctx);
			i = 0;
		}
	}
}

void vb2_sha1_finalize(struct vb2_sha1_context *ctx, uint8_t *digest)
{
	uint32_t cnt = ctx->count << 3;
	int i;

	vb2_sha1_update(ctx, (uint8_t*)"\x80", 1);
	while ((ctx->count % sizeof(ctx->buf)) != (sizeof(ctx->buf) - 8)) {
		vb2_sha1_update(ctx, (uint8_t*)"\0", 1);
	}
	for (i = 0; i < 8; ++i) {
		uint8_t tmp = (uint8_t)((uint64_t)cnt >> ((7 - i) * 8));
		vb2_sha1_update(ctx, &tmp, 1);
	}

	for (i = 0; i < 5; i++) {
		uint32_t tmp = ctx->state[i];
		*digest++ = (uint8_t)(tmp >> 24);
		*digest++ = (uint8_t)(tmp >> 16);
		*digest++ = (uint8_t)(tmp >> 8);
		*digest++ = (uint8_t)(tmp >> 0);
	}
}

#endif /* endianness */

void vb2_sha1_init(struct vb2_sha1_context *ctx)
{
	ctx->state[0] = 0x67452301;
	ctx->state[1] = 0xefcdab89;
	ctx->state[2] = 0x98badcfe;
	ctx->state[3] = 0x10325476;
	ctx->state[4] = 0xc3d2e1f0;
	ctx->count = 0;
}
