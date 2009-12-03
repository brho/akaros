#ifndef ROS_ARCH_BITMASK_H
#define ROS_ARCH_BITMASK_H

#ifndef __IVY__
#include <ros/noivy.h>
#endif

#include <string.h>
#include <sys/types.h>
#include <atomic.h>
#include <stdio.h>

#define BYTES_FOR_BITMASK(size) (size)
#define BYTES_FOR_BITMASK_WITH_CHECK(size) (size)
#define DECL_BITMASK(name, size) uint8_t (name)[BYTES_FOR_BITMASK((size))]

#define GET_BITMASK_BIT(name, bit) ((name)[(bit)])
#define SET_BITMASK_BIT(name, bit) ((name)[(bit)] = 1)
#define CLR_BITMASK_BIT(name, bit) ((name)[(bit)] = 0)
#define SET_BITMASK_BIT_ATOMIC SET_BITMASK_BIT
#define CLR_BITMASK_BIT_ATOMIC CLR_BITMASK_BIT

#define CLR_BITMASK(name, size) \
({ \
	{TRUSTEDBLOCK \
	memset((void*)((uintptr_t)(name)), 0, BYTES_FOR_BITMASK((size))); \
	} \
})

#define FILL_BITMASK(name, size) \
({ \
	{TRUSTEDBLOCK \
	memset((void*)((uintptr_t)(name)), 1, BYTES_FOR_BITMASK((size))); \
	} \
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

static inline bool BITMASK_IS_FULL(uint8_t* map, size_t size)
{
	int _size = size;
	for (int i = 0; i < BYTES_FOR_BITMASK(size); i++) {
		for (int j = 0; j < MIN(8,_size); j++)
			if(!GET_BITMASK_BIT(map, i))
				return FALSE;
			_size--;
	}
	return TRUE;
}

#define PRINT_BITMASK(name, size) { \
	int i;	\
	for (i = 0; i < BYTES_FOR_BITMASK(size); i++) { \
		printk("%x", (name)[i] );	\
	} \
	printk("\n"); \
}

static inline bool BITMASK_IS_SET_IN_RANGE(uint8_t* m, size_t beg, size_t end)
{
	for(size_t i=beg; i<end; i++) {
		if(!GET_BITMASK_BIT(m, i))
			return FALSE;
	}
	return TRUE;
}

static inline bool BITMASK_IS_CLR_IN_RANGE(uint8_t* m, size_t beg, size_t end)
{
	for(size_t i=beg; i<end; i++) {
		if(GET_BITMASK_BIT(m, i))
			return FALSE;
	}
	return TRUE;
}

static inline void SET_BITMASK_RANGE(uint8_t* m, size_t beg, size_t end)
{
	for(size_t i=beg; i<end; i++) {
		SET_BITMASK_BIT(m, i);
	}
}

static inline void CLR_BITMASK_RANGE(uint8_t* m, size_t beg, size_t end)
{
	for(size_t i=beg; i<end; i++) {
		CLR_BITMASK_BIT(m, i);
	}
}
#endif /* ROS_ARCH_BITMASK_H */

