#ifndef ROS_INC_ATOMIC_H
#define ROS_INC_ATOMIC_H

#include <arch/atomic.h>

typedef struct barrier {
	spinlock_t lock;
	uint32_t init_count;
	uint32_t current_count;
    volatile uint8_t ready;
} barrier_t;

void init_barrier(barrier_t* barrier, uint32_t count);
void reset_barrier(barrier_t* barrier);
void waiton_barrier(barrier_t* barrier);

#endif /* !ROS_INC_ATOMIC_H */
