/* See COPYRIGHT for copyright information. */

#pragma once

#include <ros/errno.h>

typedef int error_t;
extern const char *const errno_strings[];
extern const int MAX_ERRNO;

#define ERR_PTR(err)  ((void *)((intptr_t)(err)))
#define PTR_ERR(ptr)  ((intptr_t)(ptr))
#define IS_ERR(ptr)   (-(intptr_t)(ptr) <= MAX_ERRNO)

/* Plan9 wants to return non-const char* all over the place, so even if a const
 * char* would have made much more sense, unless we want to refactor a huge
 * amount of code, we need to return a char*.
 */
static inline char *errno_to_string(int error)
{
	extern const char *const errno_strings[];

	return error >= 0 && error < MAX_ERRNO && errno_strings[error] != NULL ?
		(char *) errno_strings[error]: (char *) "Unknown error";
}
