#ifndef ROS_KERN_ARCH_TIME_H
#define ROS_KERN_ARCH_TIME_H

#define TSC_HZ 1000000000 // really, this is the core clock frequency

#include <ros/common.h>

typedef struct system_timing {
	uint64_t tsc_freq;
	uint64_t timing_overhead;
} system_timing_t;

extern system_timing_t system_timing;

void timer_init(void);
void set_timer(uint32_t usec);

#endif /* ROS_KERN_ARCH_TIME_H */
