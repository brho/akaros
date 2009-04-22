/* See COPYRIGHT for copyright information. */

#ifndef ROS_KERN_SCHED_H
#define ROS_KERN_SCHED_H
#ifndef ROS_KERNEL
# error "This is a ROS kernel header; user programs should not #include it"
#endif

// This function does not return.
void sched_yield(void) __attribute__((noreturn));

#endif	// !ROS_KERN_SCHED_H
