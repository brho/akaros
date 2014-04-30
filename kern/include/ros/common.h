#ifndef ROS_COMMON_H
#define ROS_COMMON_H

#ifndef __ASSEMBLER__
#ifndef __IVY__
#include <ros/noivy.h>
#endif

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <stdbool.h>

typedef uintptr_t physaddr_t;
typedef long intreg_t;
typedef unsigned long uintreg_t;

#ifndef NULL
#define NULL ((void*) 0)
#endif

#ifndef TRUE
#define TRUE	1
#endif

#ifndef FALSE
#define FALSE	0
#endif

#define KiB	1024u
#define MiB	1048576u
#define GiB	1073741824u
#define TiB	1099511627776ull
#define PiB	1125899906842624ull
#define EiB	1152921504606846976ull

#define ALIGNED(p, a)	(!(((uintptr_t)(p)) & ((a)-1)))

#define ARRAY_SIZE(x) (sizeof((x))/sizeof((x)[0]))

#define CHECK_FLAG(flags,bit)   ((flags) & (1 << (bit)))

#define FOR_CIRC_BUFFER(next, size, var) \
	for (int _var = 0, var = (next); _var < (size); _var++, var = (var + 1) % (size))

#define STRINGIFY(s) __STRINGIFY(s)
#define __STRINGIFY(s) #s

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

/* Rounding operations (efficient when n is a power of 2)
 * Round down to the nearest multiple of n.
 * The compiler should compile out the branch.  This is needed for 32 bit, so
 * that we can round down uint64_t, without chopping off the top 32 bits. */
#define ROUNDDOWN(a, n)                                                        \
({                                                                             \
	typeof(a) __b;                                                             \
	if (sizeof(a) == 8) {                                                      \
		uint64_t __a = (uint64_t) (a);                                         \
		__b = (typeof(a)) (__a - __a % (n));                                   \
	} else {                                                                   \
		uintptr_t __a = (uintptr_t) (a);                                       \
		__b = (typeof(a)) (__a - __a % (n));                                   \
	}                                                                          \
	__b;                                                                       \
})

/* Round up to the nearest multiple of n */
#define ROUNDUP(a, n)                                                          \
({                                                                             \
	typeof(a) __b;                                                             \
	if (sizeof(a) == 8) {                                                      \
		uint64_t __n = (uint64_t) (n);                                         \
		__b = (typeof(a)) (ROUNDDOWN((uint64_t) (a) + __n - 1, __n));          \
	} else {                                                                   \
		uintptr_t __n = (uintptr_t) (n);                                       \
		__b = (typeof(a)) (ROUNDDOWN((uintptr_t) (a) + __n - 1, __n));         \
	}                                                                          \
	__b;                                                                       \
})

#define DIV_ROUND_UP(n,d) (((n) + (d) - 1) / (d))
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

static inline uintptr_t ROUNDDOWNPWR2(uintptr_t value)
{
	return 1 << LOG2_DOWN(value);
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

/* Force the reading exactly once of x.  You may still need mbs().  See
 * http://lwn.net/Articles/508991/ for more info. */
#define ACCESS_ONCE(x) (*(volatile typeof(x) *)&(x))

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

#endif /* __ASSEMBLER__ */

#endif /* ROS_COMMON_H */
