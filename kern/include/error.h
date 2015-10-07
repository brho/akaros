/* See COPYRIGHT for copyright information. */

#ifndef ROS_INC_ERROR_H
#define ROS_INC_ERROR_H

#include <ros/errno.h>

typedef int error_t;
extern const char *const errno_strings[];
extern const int MAX_ERRNO;

#define ERR_PTR(err)  ((void *)((uintptr_t)(err)))
#define PTR_ERR(ptr)  ((uintptr_t)(ptr))
#define IS_ERR(ptr)   ((uintptr_t)-(uintptr_t)(ptr) <= MAX_ERRNO)

#endif	// !ROS_INC_ERROR_H */
