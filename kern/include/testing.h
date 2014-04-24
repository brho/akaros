#ifndef ROS_INC_TESTING_H
#define ROS_INC_TESTING_H

// #include <ros/common.h>
#include <trap.h>

void test_hello_world_handler(struct hw_trapframe *hw_tf, void *data);
void test_print_info_handler(struct hw_trapframe *hw_tf, void *data);
void test_barrier_handler(struct hw_trapframe *hw_tf, void *data);

#endif /* !ROS_INC_TESTING_H */
