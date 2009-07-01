/* See COPYRIGHT for copyright information. */

#ifndef ROS_INC_ERROR_H
#define ROS_INC_ERROR_H

#define DECLARE_ERROR_CODE(e, s)

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
	NUMERRORS,               // Total number of error codes
} error_t;

/* 
 * The special format for printk %e takes an integer 
 * error code and prints a string describing the error.
 * The integer may be positive or negative,
 * so that -ENOMEM and ENOMEM are equivalent.
 */

static const char * const error_string[NUMERRORS] =
{
	"Success",
	"Generic Failure",
	"Wrong permissions",
	"Would cause deadlock",
	"Currently busy, try again later",
	"No memory available",
	"Invalid arguments"
	"Segmentation fault"
	"Bad environment"
	"No free environment"
	"Unspecified"
};

#endif	// !ROS_INC_ERROR_H */
