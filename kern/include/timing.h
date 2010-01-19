#ifndef ROS_INC_TIMING_H
#define ROS_INC_TIMING_H

#include <ros/common.h>

void udelay(uint64_t usec);

// arm the programmable interrupt timer.
// usec=0 disables it.
void set_timer(uint32_t usec);

#endif
