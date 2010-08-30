/* Copyright (c) 2010 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Endian #def and byte_swapping functions */

#ifndef ROS_KERN_ARCH_ENDIAN_H
#define ROS_KERN_ARCH_ENDIAN_H

#include <ros/common.h>
#include <arch/types.h>

static inline uint16_t byte_swap16(uint16_t x)
{
	return (uint16_t)(x << 8 | x >> 8);
}

static inline uint32_t byte_swap32(uint32_t x)
{
	/* the + denotes both output and input */
	asm volatile("bswap %0" : "+r"(x));
	return x;
}

static inline uint64_t byte_swap64(uint64_t x)
{
	return (uint64_t)(((uint64_t)byte_swap32(x & 0xffffffff) << 32) |
	                  (byte_swap32(x >> 32)));
}

#endif /* ROS_KERN_ARCH_ENDIAN_H */
