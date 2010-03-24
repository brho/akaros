#ifndef ROS_INC_TYPES_H
#define ROS_INC_TYPES_H

#include <stddef.h>

#define BIG_ENDIAN

#define NUM_ADDR_BITS 32
#define MAX_VADDR     ((uint64_t)(~0) >> (64-NUM_ADDR_BITS))

#define CHECK_FLAG(flags,bit)   ((flags) & (1 << (bit)))

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

// Pointers and addresses are 32 bits long.
// We use pointer types to represent virtual addresses,
// uintptr_t to represent the numerical values of virtual addresses,
// and physaddr_t to represent physical addresses.
typedef uint32_t uintptr_t;

#endif /* !ROS_INC_TYPES_H */
