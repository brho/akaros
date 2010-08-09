/* This exists so we can conveniently call for sys/types.h in headers shared by
 * both the kernel and userspace.  This is the kernel's version. */

#ifndef _ROS_SYS_TYPES_H
#define _ROS_SYS_TYPES_H

#include <ros/common.h>
#include <arch/types.h>

typedef uint64_t __dev_t;
typedef uint64_t __ino64_t;
typedef uint32_t __mode_t;
typedef uint32_t __nlink_t;
typedef uint32_t __uid_t;
typedef uint32_t __gid_t;
typedef int64_t __off64_t;
typedef int32_t __blksize_t;
typedef int64_t __blkcnt64_t;

#endif /* _ROS_SYS_TYPES_H */
