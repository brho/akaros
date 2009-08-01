#ifndef ROS_ARCH_SMP_H
#define ROS_ARCH_SMP_H

#include <atomic.h>

// be careful changing this, esp if you go over 16
#define NUM_HANDLER_WRAPPERS		5

typedef struct HandlerWrapper {
	checklist_t* cpu_list;
	uint8_t vector;
} handler_wrapper_t;

#endif
