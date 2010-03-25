/* See COPYRIGHT for copyright information. */

#ifndef ROS_INC_ERROR_H
#define ROS_INC_ERROR_H

#include <ros/errno.h>

typedef int error_t;

/* The special format for printk %e takes an integer
 * error code and prints a string describing the error.
 * The integer may be positive or negative,
 * so that -ENOMEM and ENOMEM are equivalent.
 */

static const char *const error_string[] =
{
	#include <errstrings.h>
};

#endif	// !ROS_INC_ERROR_H */
