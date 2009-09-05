#ifndef ROS_ARCH_TIMER_H
#define ROS_ARCH_TIMER_H

#define INTERRUPT_TIMER_HZ	100

#include <ros/common.h>

typedef struct system_timing {
	uint64_t tsc_freq;
} system_timing_t;

extern system_timing_t system_timing;

void timer_init(void);

#endif /* !ROS_ARCH_TIMER_H */
