#ifndef ROS_INC_NULL_H
#define ROS_INC_NULL_H

void null();
error_t null_async(async_desc_t** desc);
void cache_buster(uint32_t num_writes, uint32_t num_pages, uint32_t flags);
error_t cache_buster_async(async_desc_t** desc, uint32_t num_writes,
                           uint32_t num_pages, uint32_t flags);
uint32_t getcpuid(void);

#endif // ROS_INC_NULL_H
