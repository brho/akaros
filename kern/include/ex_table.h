#ifndef ROS_INC_EX_TABLE_H
#define ROS_INC_EX_TABLE_H

#include <stdint.h>

void exception_table_init(void);
uintptr_t get_fixup_ip(uintptr_t xip);

#endif /* ROS_INC_EX_TABLE_H */
