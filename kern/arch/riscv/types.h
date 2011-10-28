#ifndef ROS_INC_TYPES_H
#define ROS_INC_TYPES_H

#include <stddef.h>

#define LITTLE_ENDIAN

#ifdef __riscv64
# define NUM_ADDR_BITS 64
#else
# define NUM_ADDR_BITS 32
#endif

#define MAX_VADDR     ((uint64_t)(~0) >> (64-NUM_ADDR_BITS))

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

typedef int64_t ssize_t;
typedef int32_t pid_t;
typedef int32_t uid_t;
typedef int32_t gid_t;

typedef unsigned long uintptr_t;

#endif /* !ROS_INC_TYPES_H */
