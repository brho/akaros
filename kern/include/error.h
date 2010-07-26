/* See COPYRIGHT for copyright information. */

#ifndef ROS_INC_ERROR_H
#define ROS_INC_ERROR_H

#include <ros/errno.h>

typedef int error_t;

#define ERR_PTR(err)  ((void *)((uintptr_t)(err)))
#define PTR_ERR(ptr)  ((uintptr_t)(ptr))
#define IS_ERR(ptr)   ((uintptr_t)-(uintptr_t)(ptr) < 512)

/* The special format for printk %e takes an integer
 * error code and prints a string describing the error.
 * The integer may be positive or negative,
 * so that -ENOMEM and ENOMEM are equivalent.
 */

static const char *const error_string[] =
{
	#include <errstrings.h>
};
#define NUMERRORS (sizeof(error_string)/sizeof(error_string[0]))

#endif	// !ROS_INC_ERROR_H */
