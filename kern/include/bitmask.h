#ifndef ROS_KERN_BITMASK_H
#define ROS_KERN_BITMASK_H

#include <arch/bitmask.h>

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

/* Runs *work on every bit in the bitmask, passing *work the value of the bit
 * that is set.  Optionally clears the bit from the bitmask */
static void BITMASK_FOREACH_SET(uint8_t *name, unsigned int size,
                                void (*work)(unsigned int bit), bool clear)
{
	for (int i = 0; i < size; i++) {
		bool present = GET_BITMASK_BIT(name, i);
		if (present && clear)
			CLR_BITMASK_BIT_ATOMIC(name, i);
		if (present)
			work(i);
	}
}

#endif /* ROS_KERN_BITMASK_H */
