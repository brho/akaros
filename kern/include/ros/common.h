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

#define ROS_MEM_ALIGN 4
// Rounding operations (efficient when n is a power of 2)
// Round down to the nearest multiple of n
#define ROUNDDOWN(a, n)						\
({								\
	uintptr_t __a = (uintptr_t) (a);				\
	(typeof(a)) (__a - __a % (n));				\
})
// Round up to the nearest multiple of n
#define ROUNDUP(a, n)						\
({								\
	uintptr_t __n = (uintptr_t) (n);				\
	(typeof(a)) (ROUNDDOWN((uintptr_t) (a) + __n - 1, __n));	\
})

#define MEM_ALIGN_SIZE(size) ROUNDUP(size, ROS_MEM_ALIGN)

// Round down to the nearest multiple of n
#define PTRROUNDDOWN(a, n)						\
({								\
	char * __a = (char *) (a);				\
	(typeof(a)) (__a - (uintptr_t)__a % (n));				\
})
// Round pointer up to the nearest multiple of n
#define PTRROUNDUP(a, n)						\
({								\
	uintptr_t __n = (uintptr_t) (n);				\
	(typeof(a)) (PTRROUNDDOWN((char *) (a) + __n - 1, __n));	\
})

// Return the integer logarithm of the value provided rounded down
static inline uintptr_t LOG2_DOWN(uintptr_t value)
{
	uintptr_t l = 0;
	while( (value >> l) > 1 ) ++l;
	return l;
}

// Return the integer logarithm of the value provided rounded up
static inline uintptr_t LOG2_UP(uintptr_t value)
{
	uintptr_t _v = LOG2_DOWN(value);
	if (value ^ (1 << _v))
		return _v + 1;
	else
		return _v;
}

static inline uintptr_t ROUNDUPPWR2(uintptr_t value)
{
	return 1 << LOG2_UP(value);
}

/* We wraparound if UINT_MAX < a * b, which is also UINT_MAX / a < b. */
static inline bool mult_will_overflow_u64(uint64_t a, uint64_t b)
{
	if (!a)
		return FALSE;
	return (uint64_t)(-1) / a < b;
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

/* Makes sure func is run exactly once.  Can handle concurrent callers, and
 * other callers spin til the func is complete. */
#define run_once(func)                                                         \
{                                                                              \
	static bool ran_once = FALSE;                                              \
	static atomic_t is_running = FALSE;                                        \
	if (!ran_once) {                                                           \
		if (!atomic_swap(&is_running, TRUE)) {                                 \
			/* we won the race and get to run the func */                      \
			func;                                                              \
			wmb();	/* don't let the ran_once write pass previous writes */    \
			ran_once = TRUE;                                                   \
		} else {                                                               \
			/* someone else won, wait til they are done to break out */        \
			while (!ran_once)                                                  \
				cpu_relax();                                                   \
                                                                               \
		}                                                                      \
	}                                                                          \
}

/* Unprotected, single-threaded version, makes sure func is run exactly once */
#define run_once_racy(func)                                                    \
{                                                                              \
	static bool ran_once = FALSE;                                              \
	if (!ran_once) {                                                           \
		func;                                                                  \
		ran_once = TRUE;                                                       \
	}                                                                          \
}

/* Aborts with 'retcmd' if this function has already been called.  Compared to
 * run_once, this is put at the top of a function that can be called from
 * multiple sources but should only execute once. */
#define init_once_racy(retcmd)                                                 \
{                                                                              \
	static bool initialized = FALSE;                                           \
	if (initialized) {                                                         \
		retcmd;                                                                \
	}                                                                          \
	initialized = TRUE;                                                        \
}

#endif /* ROS_COMMON_H */
