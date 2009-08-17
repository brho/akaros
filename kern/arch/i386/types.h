#ifndef ROS_INC_TYPES_H
#define ROS_INC_TYPES_H

#define LITTLE_ENDIAN

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

// Represents true-or-false values
typedef int bool;

// Explicitly-sized versions of integer types
typedef __signed char int8_t;
typedef unsigned char uint8_t;
typedef short int16_t;
typedef unsigned short uint16_t;
typedef int int32_t;
typedef unsigned int uint32_t;
typedef long long int64_t;
typedef unsigned long long uint64_t;

// Pointers and addresses are 32 bits long.
// We use pointer types to represent virtual addresses,
// uintptr_t to represent the numerical values of virtual addresses,
// and physaddr_t to represent physical addresses.
typedef int32_t intptr_t;
typedef uint32_t uintptr_t;
typedef uint32_t physaddr_t;

// Registers are 32 bits long
typedef int32_t intreg_t;
typedef uint32_t uintreg_t;

// Page numbers are 32 bits long.
typedef uint32_t ppn_t;

// size_t is used for memory object sizes.
typedef uint32_t size_t;
// ssize_t is a signed version of ssize_t, used in case there might be an
// error return.
typedef int32_t ssize_t;

// off_t is used for file offsets and lengths.
typedef int32_t off_t;

// Efficient min and max operations
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

// Return the offset of 'member' relative to the beginning of a struct type
#ifndef offsetof
#define offsetof(type, member)  ((size_t) (&((type*)0)->member))
#endif

// Ivy currently can only handle 63 bits (OCaml thing), so use this to make
// a uint64_t programatically
#define UINT64(upper, lower) ( (((uint64_t)(upper)) << 32) | (lower) )

/*********************** Bitmask stuff **********************/
#define BYTES_FOR_BITMASK(size) (((size) - 1) / 8 + 1)
#define BYTES_FOR_BITMASK_WITH_CHECK(size) ((size) ? ((size) - (1)) / (8) + (1) : (0))
#define DECL_BITMASK(name, size) uint8_t (name)[BYTES_FOR_BITMASK((size))]

#define GET_BITMASK_BIT(name, bit) (((name)[(bit)/8] & (1 << ((bit) % 8))) ? 1 : 0)
#define SET_BITMASK_BIT(name, bit) ((name)[(bit)/8] |= (1 << ((bit) % 8)))
#define CLR_BITMASK_BIT(name, bit) ((name)[(bit)/8] &= ~(1 << ((bit) % 8)))
#define SET_BITMASK_BIT_ATOMIC(name, bit) (atomic_orb(&(name)[(bit)/8], (1 << ((bit) % 8))))
#define CLR_BITMASK_BIT_ATOMIC(name, bit) (atomic_andb(&(name)[(bit)/8], ~(1 << ((bit) % 8))))

#define CLR_BITMASK(name, size) \
({ \
	{TRUSTEDBLOCK \
	memset((void*)((uintptr_t)(name)), 0, BYTES_FOR_BITMASK((size))); \
	} \
})

#define FILL_BITMASK(name, size) \
({ \
	{TRUSTEDBLOCK \
	memset((void*)((uintptr_t)(name)), 255, BYTES_FOR_BITMASK((size))); \
	} \
	(name)[BYTES_FOR_BITMASK((size))-1] >>= (((size) % 8) ? (8 - ((size) % 8)) : 0 ); \
}) 

#define COPY_BITMASK(newmask, oldmask, size) \
({ \
	{TRUSTEDBLOCK \
	memcpy((void*)((uintptr_t)(newmask)), \
           (void*)((uintptr_t)(oldmask)), \
           BYTES_FOR_BITMASK((size))); \
	} \
})

// this checks the entire last byte, so keep it 0 in the other macros
#define BITMASK_IS_CLEAR(name, size) ({ \
	uint32_t __n = BYTES_FOR_BITMASK((size)); \
	bool clear = 1; \
	while (__n-- > 0) { \
		if ((name)[__n]) { \
			clear = 0; \
			break;\
		}\
	} \
	clear; })

#define PRINT_BITMASK(name, size) { \
	int i;	\
	for (i = 0; i < BYTES_FOR_BITMASK(size); i++) { \
		int j;	\
		for (j = 0; j < 8; j++) \
			printk("%x", ((name)[i] >> j) & 1);	\
	} \
	printk("\n"); \
}
/**************************************************************/

#endif /* !ROS_INC_TYPES_H */
