#ifndef ROS_INC_TYPES_H
#define ROS_INC_TYPES_H

#include <stddef.h>
#ifndef LITTLE_ENDIAN
#define LITTLE_ENDIAN
#endif /* !LITTLE_ENDIAN */

//Constants for byte sizes
#define ONE_KILOBYTE  (1L<<10)
#define ONE_MEGABYTE  (1L<<20)
#define ONE_GIGABYTE  (1L<<30)

// Explicitly-sized versions of integer types
typedef __signed char int8_t;
typedef unsigned char uint8_t;
typedef short int16_t;
typedef unsigned short uint16_t;
typedef int int32_t;
typedef unsigned int uint32_t;
typedef long long int64_t;
typedef unsigned long long uint64_t;

typedef long ssize_t;
typedef int pid_t;
typedef int uid_t;
typedef int gid_t;

#ifdef CONFIG_X86_64

#define NUM_ADDR_BITS 64
#define BITS_PER_LONG 64
#define MAX_VADDR     ((uint64_t)(~0) >> (64-NUM_ADDR_BITS))
typedef uint64_t uintptr_t;
#define PAGE_SHIFT 12
#define PAGE_SIZE (1<<PAGE_SHIFT)
#define PAGE_MASK 0xFFFFFFFFfffff000
#else /* 32 bit */

#define NUM_ADDR_BITS 32
#define BITS_PER_LONG 32
#define MAX_VADDR     ((uint64_t)(~0) >> (64-NUM_ADDR_BITS))
typedef uint32_t uintptr_t;

#define PAGE_SHIFT 12
#define PAGE_SIZE (1<<PAGE_SHIFT)
#define PAGE_MASK 0xFFFFF000
#endif /* 64bit / 32bit */


#endif /* !ROS_INC_TYPES_H */
