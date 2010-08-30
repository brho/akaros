/* Copyright (c) 2010 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Macros to convert to and from endian-data */

#include <ros/common.h>
#include <arch/endian.h>

#ifndef ROS_KERN_ENDIAN_H
#define ROS_KERN_ENDIAN_H

#ifdef LITTLE_ENDIAN
#define cpu_to_le16(x) ((uint16_t)(x))
#define cpu_to_le32(x) ((uint32_t)(x))
#define cpu_to_le64(x) ((uint64_t)(x))
#define le16_to_cpu(x) ((uint16_t)(x))
#define le32_to_cpu(x) ((uint32_t)(x))
#define le64_to_cpu(x) ((uint64_t)(x))

#define cpu_to_be16(x) byte_swap16((x))
#define cpu_to_be32(x) byte_swap32((x))
#define cpu_to_be64(x) byte_swap64((x))
#define be16_to_cpu(x) byte_swap16((x))
#define be32_to_cpu(x) byte_swap32((x))
#define be64_to_cpu(x) byte_swap64((x))

#else /* big endian */

# ifndef BIG_ENDIAN
# error "Need an endianness defined"
# endif

#define cpu_to_le16(x) byte_swap16((x))
#define cpu_to_le32(x) byte_swap32((x))
#define cpu_to_le64(x) byte_swap64((x))
#define le16_to_cpu(x) byte_swap16((x))
#define le32_to_cpu(x) byte_swap32((x))
#define le64_to_cpu(x) byte_swap64((x))

#define cpu_to_be16(x) ((uint16_t)(x))
#define cpu_to_be32(x) ((uint32_t)(x))
#define cpu_to_be64(x) ((uint64_t)(x))
#define be16_to_cpu(x) ((uint16_t)(x))
#define be32_to_cpu(x) ((uint32_t)(x))
#define be64_to_cpu(x) ((uint64_t)(x))

#endif /* endian */

#endif /* ROS_KERN_ENDIAN_H */
