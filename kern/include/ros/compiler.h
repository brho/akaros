#ifndef ROS_KERN_COMPILER_H
#define ROS_KERN_COMPILER_H

#ifdef __GNUC__

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#else /* #ifdef __GNUC__ */

#define likely(x) (x)
#define unlikely(x) (x)

#endif /* #ifdef __GNUC__ */

#endif /* ROS_KERN_COMPILER_H */
