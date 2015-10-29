#pragma once

// #include <ros/common.h>
#include <trap.h>

void test_hello_world_handler(struct hw_trapframe *hw_tf, void *data);
void test_barrier_handler(struct hw_trapframe *hw_tf, void *data);
