#ifndef ROS_INCLUDE_SYSCALL_H
#define ROS_INCLUDE_SYSCALL_H

#include <ros/bits/syscall.h>

// convenience wrapper for __ros_syscall
#define ros_syscall(which,a0,a1,a2,a3,a4) \
   __ros_syscall(which,(long)(a0),(long)(a1),(long)(a2),(long)(a3),(long)(a4))
#include <ros/arch/syscall.h>

#endif /* !ROS_INCLUDE_SYSCALL_H */
