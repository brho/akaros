#pragma once

#include <stddef.h>
#ifndef LITTLE_ENDIAN
#define LITTLE_ENDIAN
#endif /* !LITTLE_ENDIAN */

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

#define NUM_ADDR_BITS 64
#define BITS_PER_LONG 64
#define MAX_VADDR     ((uint64_t)(~0) >> (64-NUM_ADDR_BITS))
typedef uint64_t uintptr_t;
typedef int64_t intptr_t;
#define PAGE_SHIFT 12
#define PAGE_SIZE (1<<PAGE_SHIFT)
#define PAGE_MASK 0xFFFFFFFFfffff000
