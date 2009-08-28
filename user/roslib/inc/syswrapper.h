#ifndef ROS_INC_SYSWRAPPER_H
#define ROS_INC_SYSWRAPPER_H

#include <lib.h>

void null();
error_t null_async(async_desc_t** desc);
void cache_buster(uint32_t num_writes, uint32_t num_pages, uint32_t flags);
error_t cache_buster_async(async_desc_t** desc, uint32_t num_writes,
                           uint32_t num_pages, uint32_t flags);
uint32_t getcpuid(void);
void yield(void);
int proc_create(char *NT path);
error_t proc_run(int pid);

#endif // ROS_INC_SYSWRAPPER_H
