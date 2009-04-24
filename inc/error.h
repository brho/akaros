/* See COPYRIGHT for copyright information. */

#ifndef ROS_INC_ERROR_H
#define ROS_INC_ERROR_H

#define TRUE	1
#define FALSE	0

typedef enum {
	E_FAIL		=	-1,
	E_SUCCESS	=	0,
} error_t;

// Kernel error codes -- keep in sync with list in lib/printfmt.c.
#define E_UNSPECIFIED	1	// Unspecified or unknown problem
#define E_BAD_ENV		2	// Environment doesn't exist or otherwise
							// cannot be used in requested action
#define E_INVAL			3	// Invalid parameter
#define E_NO_MEM		4	// Request failed due to memory shortage
#define E_NO_FREE_ENV	5	// Attempt to create a new environment beyond
							// the maximum allowed
#define E_FAULT			6	// Memory fault
#define	MAXERROR		6

#endif	// !ROS_INC_ERROR_H */
