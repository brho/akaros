#ifndef ROS_ARCH_TIMER_H
#define ROS_ARCH_TIMER_H

#define TSC_HZ 1000000000 // really, this is the core clock frequency

#include <ros/common.h>

typedef struct system_timing {
	uint64_t tsc_freq;
} system_timing_t;

extern system_timing_t system_timing;

void timer_init(void);
void set_timer(uint32_t usec);

#endif /* !ROS_ARCH_TIMER_H */
