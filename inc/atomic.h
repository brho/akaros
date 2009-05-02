#ifndef ROS_INC_ATOMIC_H
#define ROS_INC_ATOMIC_H

// TODO - check these, wrt x86
#define mb() {rmb(); wmb();}
#define rmb() ({ asm volatile("lfence"); })
#define wmb() 

#endif /* !ROS_INC_ATOMIC_H */
