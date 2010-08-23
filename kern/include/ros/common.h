#ifndef ROS_COMMON_H
#define ROS_COMMON_H

#ifndef __IVY__
#include <ros/noivy.h>
#endif

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <stdbool.h>

typedef uintptr_t physaddr_t;
typedef ssize_t intreg_t;
typedef size_t uintreg_t;

#ifndef NULL
#define NULL ((void*) 0)
#endif

#ifndef TRUE
#define TRUE	1
#endif

#ifndef FALSE
#define FALSE	0
#endif

#define CHECK_FLAG(flags,bit)   ((flags) & (1 << (bit)))

#define FOR_CIRC_BUFFER(next, size, var) \
	for (int _var = 0, var = (next); _var < (size); _var++, var = (var + 1) % (size))

// Efficient min and max operations
#ifdef ROS_KERNEL /* Glibc has their own */
#define MIN(_a, _b)						\
({								\
	typeof(_a) __a = (_a);					\
	typeof(_b) __b = (_b);					\
	__a <= __b ? __a : __b;					\
})
#define MAX(_a, _b)						\
({								\
	typeof(_a) __a = (_a);					\
	typeof(_b) __b = (_b);					\
	__a >= __b ? __a : __b;					\
})
#endif

// Rounding operations (efficient when n is a power of 2)
// Round down to the nearest multiple of n
#define ROUNDDOWN(a, n)						\
({								\
	uint32_t __a = (uint32_t) (a);				\
	(typeof(a)) (__a - __a % (n));				\
})
// Round up to the nearest multiple of n
#define ROUNDUP(a, n)						\
({								\
	uint32_t __n = (uint32_t) (n);				\
	(typeof(a)) (ROUNDDOWN((uint32_t) (a) + __n - 1, __n));	\
})

// Round down to the nearest multiple of n
#define PTRROUNDDOWN(a, n)						\
({								\
	char * __a = (char *) (a);				\
	(typeof(a)) (__a - (uint32_t)__a % (n));				\
})
// Round pointer up to the nearest multiple of n
#define PTRROUNDUP(a, n)						\
({								\
	uint32_t __n = (uint32_t) (n);				\
	(typeof(a)) (PTRROUNDDOWN((char *) (a) + __n - 1, __n));	\
})

// Return the integer logarithm of the value provided rounded down
static inline uint32_t LOG2_DOWN(uint32_t value)
{
	uint32_t l = 0;
	while( (value >> l) > 1 ) ++l;
	return l;
}

// Return the integer logarithm of the value provided rounded up
static inline uint32_t LOG2_UP(uint32_t value)
{
	uint32_t _v = LOG2_DOWN(value);
	if (value ^ (1 << _v))
		return _v + 1;
	else
		return _v;
}

static inline uint32_t ROUNDUPPWR2(uint32_t value)
{
	return 1 << LOG2_UP(value);
}

// Return the offset of 'member' relative to the beginning of a struct type
#ifndef offsetof
#define offsetof(type, member)  ((size_t) (&((type*)0)->member))
#endif

/* Return the container/struct holding the object 'ptr' points to */
#define container_of(ptr, type, member) ({                                     \
	(type*)((char*)ptr - offsetof(type, member));                             \
})

// Ivy currently can only handle 63 bits (OCaml thing), so use this to make
// a uint64_t programatically
#define UINT64(upper, lower) ( (((uint64_t)(upper)) << 32) | (lower) )

#endif /* ROS_COMMON_H */
