#pragma once

#define TSC_HZ 1000000000 // really, this is the core clock frequency

#include <ros/common.h>

void timer_init(void);
void set_timer(uint32_t usec);
