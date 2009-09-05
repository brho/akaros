/* See COPYRIGHT for copyright information. */

#ifndef ROS_INC_ERROR_H
#define ROS_INC_ERROR_H

#include <ros/common.h>

#define DECLARE_ERROR_CODE(e, s)

// define this to prevent conflicts with newlib's errno.h
#define __error_t_defined
typedef enum {
	ESUCCESS = 0,            // Success
	EFAIL,                   // Generic Failure
	EPERM,                   // Wrong permissions
	EDEADLOCK,               // Would cause deadlock
	EBUSY,                   // Currently busy, try again later
	ENOMEM,                  // No memory available
	EINVAL,                  // Invalid arguments
	EFAULT,                  // Segmentation fault
	EBADENV,                 // Bad environment 
	ENOFREEENV,              // No free environment
	EUNSPECIFIED,            // Unspecified
	EMORON,                  // Moron
	NUMERRORS,               // Total number of error codes
} error_t;

/* 
 * The special format for printk %e takes an integer 
 * error code and prints a string describing the error.
 * The integer may be positive or negative,
 * so that -ENOMEM and ENOMEM are equivalent.
 */

static const char *NTS const error_string[NUMERRORS] =
{
	"Success",
	"Generic Failure",
	"Wrong permissions",
	"Would cause deadlock",
	"Currently busy, try again later",
	"No memory available",
	"Invalid arguments",
	"Segmentation fault",
	"Bad environment",
	"No free environment",
	"You are a moron",
	"Unspecified",
};

#endif	// !ROS_INC_ERROR_H */
