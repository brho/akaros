
#ifndef ROS_KERN_INC_PROFILER_H
#define ROS_KERN_INC_PROFILER_H

#include <sys/types.h>
#include <trap.h>

int profiler_init(void);
void profiler_cleanup(void);
void profiler_add_backtrace(uintptr_t pc, uintptr_t fp);
void profiler_add_userpc(uintptr_t pc);
void profiler_add_trace(uintptr_t eip);
void profiler_control_trace(int onoff);
void profiler_add_hw_sample(struct hw_trapframe *hw_tf);
int profiler_read(void *va, int);
int profiler_size(void);

#endif /* ROS_KERN_INC_PROFILER_H */
