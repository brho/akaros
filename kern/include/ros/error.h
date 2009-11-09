/* See COPYRIGHT for copyright information. */

#ifndef ROS_INC_ERROR_H
#define ROS_INC_ERROR_H

#include <ros/common.h>

#define DECLARE_ERROR_CODE(e, s)

// define this to prevent conflicts with newlib's errno.h
#define __error_t_defined
#if 0
#define ESUCCESS			 0		// Success
#define EFAIL				 1		// Generic Failure
#define EPERM				 2		// Wrong permissions
#define EDEADLOCK			 3		// Would cause deadlock
#define EBUSY				 4		// Currently busy, try again later
#define ENOMEM				 5		// No memory available
#define ENOCACHE			 6		// No memory available
#define EINVAL				 7		// Invalid arguments
#define EFAULT				 8		// Segmentation fault
#define EBADPROC			 9		// Bad process
#define ENOFREEPID			10		// No free pid
#define EUNSPECIFIED		11		// Unspecified
#define EMORON				12		// Moron
#define NUMERRORS			13		// Total number of error codes
#endif

/* this enum is ghetto, but #defining collides with newlib.  right now, we
 * collide on names *and* numbers, which needs to be sorted.  (TODO) */
enum {
	ESUCCESS,
	EFAIL,
	EPERM,
	EDEADLOCK,
	EBUSY,
	ENOMEM,
	ENOCACHE,
	EINVAL,
	EFAULT,
	EBADPROC,
	ENOFREEPID,
	EUNSPECIFIED,
	EMORON,
	NUMERRORS,
};

typedef int error_t;

/* The special format for printk %e takes an integer
 * error code and prints a string describing the error.
 * The integer may be positive or negative,
 * so that -ENOMEM and ENOMEM are equivalent.
 */

static const char *NTS const (RO error_string)[NUMERRORS] =
{
	"Success",
	"Generic Failure",
	"Wrong permissions",
	"Would cause deadlock",
	"Currently busy, try again later",
	"No memory available",
	"No cache available",
	"Invalid arguments",
	"Segmentation fault",
	"Bad process",
	"No free pid",
	"Unspecified",
	"You are a moron",
};

#endif	// !ROS_INC_ERROR_H */
