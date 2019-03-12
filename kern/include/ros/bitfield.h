/* Copyright (c) 2015 Google Inc
 * Davide Libenzi <dlibenzi@google.com>
 * See LICENSE for details.
 *
 * This file provide an interface for dealing with bitfields, without
 * having to explicitly create by hand masks and shifts definitions
 * and operational macros.
 * Example use:
 *
 * #define MYBF_X MKBITFIELD(0, 4)
 * #define MYBF_Y MKBITFIELD(4, 3)
 * #define MYBF_Z MKBITFIELD(7, 25)
 *
 * #define MYBF_GET_X(v)    BF_GETFIELD(v, MYBF_X)
 * #define MYBF_SET_X(v, x) BF_SETFIELD(v, x, MYBF_X)
 * #define MYBF_GET_Y(v)    BF_GETFIELD(v, MYBF_Y)
 * #define MYBF_SET_Y(v, x) BF_SETFIELD(v, x, MYBF_Y)
 * #define MYBF_GET_Z(v)    BF_GETFIELD(v, MYBF_Z)
 * #define MYBF_SET_Z(v, x) BF_SETFIELD(v, x, MYBF_Z)
 */

#pragma once

#include <stdint.h>

struct bitfield_conf {
    uint8_t shift;
    uint8_t nbits;
};

#define MKBITFIELD(s, n) \
    ((struct bitfield_conf) { .shift = (s), .nbits = (n) })

#define BF_MKMASK(type, bfc) ((((type) 1 << bfc.nbits) - 1) << bfc.shift)

#define BF_GETFIELD(val, bfc) \
	({ ((val) >> bfc.shift) & (((typeof(val)) 1 << bfc.nbits) - 1); })

#define BF_SETFIELD(val, x, bfc)					\
	({ typeof(val) m = BF_MKMASK(typeof(val), bfc);	 		\
		(val) = ((val) & ~m) |					\
			(((typeof(val)) (x) << bfc.shift) & m); })
